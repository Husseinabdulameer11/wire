title: A decimal digit counter
cycles: 400
random: rst=0.01 en=0.8
---- question
Count in decimal: 0, 1 ... 9 and back to 0, as one digit of a clock or a stopwatch does.

- `rst` = 1 at an edge: back to 0
- `en` = 1 at an edge: count one up, and after 9 comes 0
- `q` is the digit (4 bits), and `tick` is 1 while the digit is 9 **and** `en` is 1: that is the moment the next digit should count, so digits can be chained.
---- hint
Like the 4-bit counter, but `if count = 9 then count <= (others => '0'); else count <= count + 1; end if;` when enabled. `tick <= '1' when count = 9 and en = '1' else '0';`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bcd_counter is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    en   : in  std_logic;
    q    : out std_logic_vector(3 downto 0);
    tick : out std_logic
  );
end bcd_counter;

architecture rtl of bcd_counter is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bcd_counter is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    en   : in  std_logic;
    q    : out std_logic_vector(3 downto 0);
    tick : out std_logic
  );
end bcd_counter;

architecture rtl of bcd_counter is
    signal count : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                count <= (others => '0');
            elsif en = '1' then
                if count = 9 then
                    count <= (others => '0');
                else
                    count <= count + 1;
                end if;
            end if;
        end if;
    end process;
    q    <= std_logic_vector(count);
    tick <= '1' when count = 9 and en = '1' else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bcd_counter is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    en   : in  std_logic;
    q    : out std_logic_vector(3 downto 0);
    tick : out std_logic
  );
end bcd_counter;

architecture rtl of bcd_counter is
    signal count : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                count <= (others => '0');
            elsif en = '1' then
                if count = 10 then
                    count <= (others => '0');
                else
                    count <= count + 1;
                end if;
            end if;
        end if;
    end process;
    q    <= std_logic_vector(count);
    tick <= '1' when count = 9 and en = '1' else '0';
end rtl;
