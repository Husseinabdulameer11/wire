title: An electronic dice
cycles: 200
random: rst=0.03
---- question
A dice counts 1, 2, 3, 4, 5, 6, 1, 2 ... changing at every rising edge, so fast that wherever you stop it is a surprise. `rst` sets it to 1, and it never shows 0 or 7.
---- hint
`if rst = '1' or c = 6 then c <= to_unsigned(1, 3); else c <= c + 1; end if;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dice is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(2 downto 0)
  );
end dice;

architecture rtl of dice is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dice is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(2 downto 0)
  );
end dice;

architecture rtl of dice is
    signal c : unsigned(2 downto 0) := to_unsigned(1, 3);
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' or c = 6 then
                c <= to_unsigned(1, 3);
            else
                c <= c + 1;
            end if;
        end if;
    end process;
    q <= std_logic_vector(c);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dice is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(2 downto 0)
  );
end dice;

architecture rtl of dice is
    signal c : unsigned(2 downto 0) := to_unsigned(1, 3);
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' or c = 7 then
                c <= to_unsigned(1, 3);
            else
                c <= c + 1;
            end if;
        end if;
    end process;
    q <= std_logic_vector(c);
end rtl;
