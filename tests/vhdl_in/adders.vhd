-- Chips from chips: a full adder from two half adders, an N-bit
-- ripple adder from full adders, used at 4 and 8 bits.
library ieee;
use ieee.std_logic_1164.all;

library ieee;
use ieee.std_logic_1164.all;
entity half_adder is
  port(a, b : in std_logic; s, c : out std_logic);
end half_adder;
architecture rtl of half_adder is
begin
  s <= a xor b;
  c <= a and b;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
entity full_adder is
  port(a, b, cin : in std_logic; s, cout : out std_logic);
end full_adder;
architecture rtl of full_adder is
  signal s1, c1, c2 : std_logic;
begin
  h1: entity work.half_adder port map(a => a, b => b, s => s1, c => c1);
  h2: entity work.half_adder port map(s1, cin, s, c2);
  cout <= c1 or c2;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
entity ripple is
  generic(N : integer := 4);
  port(x, y : in  std_logic_vector(N-1 downto 0);
       cin  : in  std_logic := '0';
       sum  : out std_logic_vector(N-1 downto 0);
       cout : out std_logic);
end ripple;
architecture rtl of ripple is
  signal c : std_logic_vector(N downto 0);
begin
  c(0) <= cin;
  bits: for i in 0 to N-1 generate
    fa: entity work.full_adder port map(a => x(i), b => y(i), cin => c(i), s => sum(i), cout => c(i+1));
  end generate;
  cout <= c(N);
end rtl;

library ieee;
use ieee.std_logic_1164.all;
entity adders is
  port(a4, b4 : in std_logic_vector(3 downto 0);
       a8, b8 : in std_logic_vector(7 downto 0);
       ci     : in std_logic;
       s4     : out std_logic_vector(3 downto 0);
       s8     : out std_logic_vector(7 downto 0);
       c4, c8 : out std_logic);
end adders;
architecture rtl of adders is
begin
  small: entity work.ripple port map(x => a4, y => b4, sum => s4, cout => c4);
  big: entity work.ripple generic map(N => 8) port map(x => a8, y => b8, cin => ci, sum => s8, cout => c8);
end rtl;
