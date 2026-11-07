title: Debouncing a button
cycles: 500
random: rst=0.005 btn=0.75
---- question
A real button bounces: for a moment after you press it, it flickers between 0 and 1. A **debouncer** ignores short flickers.

`clean` changes to the value of `btn` only once `btn` has been **different** from `clean` at **4 rising edges in a row**. If `btn` goes back to agreeing with `clean` before that, the count starts again. `rst` sets `clean` to 0 and forgets the count.

So `clean` follows `btn`, but only after `btn` has been steady for 4 cycles.
---- hint
Count with a 2-bit `n`: on each edge, if `btn = c` then `n <= 0`; elsif `n = 3` then `c <= btn; n <= 0`; else `n <= n + 1`. `clean <= c;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity debounce is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    btn   : in  std_logic;
    clean : out std_logic
  );
end debounce;

architecture rtl of debounce is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity debounce is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    btn   : in  std_logic;
    clean : out std_logic
  );
end debounce;

architecture rtl of debounce is
    signal c : std_logic := '0';
    signal n : unsigned(1 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                c <= '0';
                n <= (others => '0');
            elsif btn = c then
                n <= (others => '0');
            elsif n = 3 then
                c <= btn;
                n <= (others => '0');
            else
                n <= n + 1;
            end if;
        end if;
    end process;
    clean <= c;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity debounce is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    btn   : in  std_logic;
    clean : out std_logic
  );
end debounce;

architecture rtl of debounce is
    signal c : std_logic := '0';
    signal n : unsigned(1 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                c <= '0';
                n <= (others => '0');
            elsif btn = c then
                n <= (others => '0');
            elsif n = 2 then
                c <= btn;
                n <= (others => '0');
            else
                n <= n + 1;
            end if;
        end if;
    end process;
    clean <= c;
end rtl;
