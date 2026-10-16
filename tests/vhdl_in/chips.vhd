-- Chips: positional port maps, a generic map by position, if ... generate,
-- open outputs, a chip used inside a generate with a signal declared in it,
-- and a chip whose name is a word of Wire (mux).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity mux is
  generic(W : integer := 1);
  port(d0, d1 : in std_logic_vector(W-1 downto 0); s : in std_logic;
       y : out std_logic_vector(W-1 downto 0));
end mux;
architecture rtl of mux is
begin
  y <= d1 when s = '1' else d0;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity parity is
  generic(N : integer := 4; ODD : integer := 0);
  port(x : in std_logic_vector(N-1 downto 0); p, unused : out std_logic);
end parity;
architecture rtl of parity is
  signal acc : std_logic_vector(N downto 0);
begin
  acc(0) <= '1' when ODD = 1 else '0';
  chain: for i in 0 to N-1 generate
    signal t : std_logic;
  begin
    t <= acc(i) xor x(i);
    acc(i+1) <= t;
  end generate;
  p <= acc(N);
  odd_only: if ODD = 1 generate
    unused <= '1';
  end generate;
  even_only: if ODD = 0 generate
    unused <= '0';
  end generate;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity chips is
  port(a, b : in std_logic_vector(7 downto 0); sel : in std_logic;
       y : out std_logic_vector(7 downto 0); pe, po : out std_logic;
       bit0 : out std_logic);
end chips;
architecture rtl of chips is
  signal one_y : std_logic_vector(0 downto 0);
begin
  wide: entity work.mux generic map(8) port map(a, b, sel, y);
  one: entity work.mux port map(d0 => a(0 downto 0), d1 => b(0 downto 0), s => sel, y => one_y);
  bit0 <= one_y(0);
  ev: entity work.parity generic map(N => 8) port map(x => a, p => pe, unused => open);
  od: entity work.parity generic map(8, 1) port map(x => b, p => po);
end rtl;
