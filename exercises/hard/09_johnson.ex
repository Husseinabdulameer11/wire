title: A Johnson counter
cycles: 200
random: rst=0.02
---- question
A **Johnson counter** is a shift register whose bit 3, turned round, comes back in at bit 0. From all zeros, a 4-bit one goes:

0000, 0001, 0011, 0111, 1111, 1110, 1100, 1000, 0000 ...

8 states from 4 flip-flops, and only one bit changes at each step. `rst` sets it to 0000.
---- hint
`s <= s(2 downto 0) & not s(3);` on each edge, and `q <= s;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity johnson4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end johnson4;

architecture rtl of johnson4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity johnson4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end johnson4;

architecture rtl of johnson4 is
    signal s : std_logic_vector(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                s <= (others => '0');
            else
                s <= s(2 downto 0) & not s(3);
            end if;
        end if;
    end process;
    q <= s;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity johnson4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end johnson4;

architecture rtl of johnson4 is
    signal s : std_logic_vector(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                s <= (others => '0');
            else
                s <= s(2 downto 0) & s(3);
            end if;
        end if;
    end process;
    q <= s;
end rtl;
