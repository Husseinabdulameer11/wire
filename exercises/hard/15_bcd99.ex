title: Counting 00 to 99
cycles: 600
random: rst=0.002 en=0.9
---- question
Two decimal digits, as on a stopwatch: `ones` counts 0 to 9, and when it goes from 9 back to 0, `tens` goes up by one. After 99 comes 00.

It counts at a rising edge only while `en` is 1; `rst` (at a rising edge) sets both digits to 0.
---- hint
Inside `elsif en = '1' then`: `if o = 9 then o <= 0; if t = 9 then t <= 0; else t <= t + 1; end if; else o <= o + 1; end if;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bcd99 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    en   : in  std_logic;
    tens : out std_logic_vector(3 downto 0);
    ones : out std_logic_vector(3 downto 0)
  );
end bcd99;

architecture rtl of bcd99 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bcd99 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    en   : in  std_logic;
    tens : out std_logic_vector(3 downto 0);
    ones : out std_logic_vector(3 downto 0)
  );
end bcd99;

architecture rtl of bcd99 is
    signal o, t : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                o <= (others => '0');
                t <= (others => '0');
            elsif en = '1' then
                if o = 9 then
                    o <= (others => '0');
                    if t = 9 then
                        t <= (others => '0');
                    else
                        t <= t + 1;
                    end if;
                else
                    o <= o + 1;
                end if;
            end if;
        end if;
    end process;
    ones <= std_logic_vector(o);
    tens <= std_logic_vector(t);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bcd99 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    en   : in  std_logic;
    tens : out std_logic_vector(3 downto 0);
    ones : out std_logic_vector(3 downto 0)
  );
end bcd99;

architecture rtl of bcd99 is
    signal o, t : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                o <= (others => '0');
                t <= (others => '0');
            elsif en = '1' then
                if o = 9 then
                    o <= (others => '0');
                    t <= t + 1;
                else
                    o <= o + 1;
                end if;
            end if;
        end if;
    end process;
    ones <= std_logic_vector(o);
    tens <= std_logic_vector(t);
end rtl;
