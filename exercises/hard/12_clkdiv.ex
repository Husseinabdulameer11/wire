title: Dividing a clock by ten
cycles: 300
random: rst=0.005
---- question
Boards have a fast clock, and slower things (a blinking LED, a stopwatch) need a slower one. Make `slow` a square wave with **one tenth** of the clock's frequency: it stays 0 for 5 cycles, then 1 for 5 cycles, and so on.

At a rising edge, a counter goes 0 to 4; when it is 4, it goes back to 0 and `slow` flips. `rst` sets the counter and `slow` to 0.
---- hint
`if rst = '1' then n <= 0; s <= '0'; elsif n = 4 then n <= 0; s <= not s; else n <= n + 1; end if;` and `slow <= s;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity div10 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    slow : out std_logic
  );
end div10;

architecture rtl of div10 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity div10 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    slow : out std_logic
  );
end div10;

architecture rtl of div10 is
    signal n : unsigned(2 downto 0) := (others => '0');
    signal s : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                n <= (others => '0');
                s <= '0';
            elsif n = 4 then
                n <= (others => '0');
                s <= not s;
            else
                n <= n + 1;
            end if;
        end if;
    end process;
    slow <= s;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity div10 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    slow : out std_logic
  );
end div10;

architecture rtl of div10 is
    signal n : unsigned(2 downto 0) := (others => '0');
    signal s : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                n <= (others => '0');
                s <= '0';
            elsif n = 5 then
                n <= (others => '0');
                s <= not s;
            else
                n <= n + 1;
            end if;
        end if;
    end process;
    slow <= s;
end rtl;
