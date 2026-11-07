title: A 4-bit counter
cycles: 300
random: rst=0.02 en=0.8
---- question
A **counter** is a register that adds 1 to itself on every clock edge. This one counts 0, 1, 2 ... 15 and then wraps round to 0.

- `rst` = 1 at an edge: back to 0
- `en` = 1 at an edge: count one up; when `en` is 0 it stays where it is
- `q` is the count, and `carry` is 1 while the count is 15

An output cannot be read inside the architecture, so keep the count in a **signal** (declared between `architecture` and `begin`), count that, and connect it to `q`. The starter declares one for you: `count`, an `unsigned` 4-bit number, so `count + 1` works.
---- hint
In the process: `if rst = '1' then count <= (others => '0'); elsif en = '1' then count <= count + 1; end if;`. Outside it: `q <= std_logic_vector(count);` and `carry <= '1' when count = 15 else '0';`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity counter4 is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    en    : in  std_logic;
    q     : out std_logic_vector(3 downto 0);
    carry : out std_logic
  );
end counter4;

architecture rtl of counter4 is
    signal count : unsigned(3 downto 0) := (others => '0');
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity counter4 is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    en    : in  std_logic;
    q     : out std_logic_vector(3 downto 0);
    carry : out std_logic
  );
end counter4;

architecture rtl of counter4 is
    signal count : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                count <= (others => '0');
            elsif en = '1' then
                count <= count + 1;
            end if;
        end if;
    end process;
    q     <= std_logic_vector(count);
    carry <= '1' when count = 15 else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity counter4 is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    en    : in  std_logic;
    q     : out std_logic_vector(3 downto 0);
    carry : out std_logic
  );
end counter4;

architecture rtl of counter4 is
    signal count : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                count <= (others => '0');
            elsif en = '1' then
                count <= count + 1;
            end if;
        end if;
    end process;
    q     <= std_logic_vector(count);
    carry <= '1' when count = 14 else '0';
end rtl;
