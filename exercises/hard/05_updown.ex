title: An up/down counter that can be loaded
cycles: 500
random: rst=0.01 load=0.06 en=0.8 up=0.6
---- question
A 4-bit counter with every feature, as in a real design. On each clock edge, the first of these that applies happens:

1. `rst` = 1: the count becomes 0
2. `load` = 1: the count becomes `d`
3. `en` = 1: the count goes one up when `up` = 1, one down when `up` = 0 (0 down is 15, 15 up is 0)
4. otherwise it stays

`q` is the count; `max` is 1 while it is 15 and `min` while it is 0.
---- hint
An `if` / `elsif` chain in the order of the list. With `unsigned`, `count - 1` from 0 wraps to 15 by itself.
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity updown4 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    load : in  std_logic;
    en   : in  std_logic;
    up   : in  std_logic;
    d    : in  std_logic_vector(3 downto 0);
    q    : out std_logic_vector(3 downto 0);
    max  : out std_logic;
    min  : out std_logic
  );
end updown4;

architecture rtl of updown4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity updown4 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    load : in  std_logic;
    en   : in  std_logic;
    up   : in  std_logic;
    d    : in  std_logic_vector(3 downto 0);
    q    : out std_logic_vector(3 downto 0);
    max  : out std_logic;
    min  : out std_logic
  );
end updown4;

architecture rtl of updown4 is
    signal count : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                count <= (others => '0');
            elsif load = '1' then
                count <= unsigned(d);
            elsif en = '1' then
                if up = '1' then
                    count <= count + 1;
                else
                    count <= count - 1;
                end if;
            end if;
        end if;
    end process;
    q   <= std_logic_vector(count);
    max <= '1' when count = 15 else '0';
    min <= '1' when count = 0 else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity updown4 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    load : in  std_logic;
    en   : in  std_logic;
    up   : in  std_logic;
    d    : in  std_logic_vector(3 downto 0);
    q    : out std_logic_vector(3 downto 0);
    max  : out std_logic;
    min  : out std_logic
  );
end updown4;

architecture rtl of updown4 is
    signal count : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                count <= (others => '0');
            elsif en = '1' then
                if up = '1' then
                    count <= count + 1;
                else
                    count <= count - 1;
                end if;
            elsif load = '1' then
                count <= unsigned(d);
            end if;
        end if;
    end process;
    q   <= std_logic_vector(count);
    max <= '1' when count = 15 else '0';
    min <= '1' when count = 0 else '0';
end rtl;
