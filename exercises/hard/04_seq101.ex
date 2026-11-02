title: Finding 101 in a bit stream
cycles: 400
random: rst=0.01 x=0.5
---- question
One bit arrives on `x` at every clock edge. `found` must be 1 when the **last three bits received**, oldest first, were 1, 0, 1. Sequences may overlap: in 1 0 1 0 1, the pattern is found twice.

`found` changes only on clock edges (it is 1 during the cycle after the third bit of a 101 arrived). `rst` forgets every bit received so far.

You can do this with a state machine, or by keeping the last three bits.
---- hint
Keep the last bits in a 3-bit signal: on each edge `last <= last(1 downto 0) & x;` and `found <= '1' when last = "101" else '0';` (with `rst` clearing `last`).
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity seq101 is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    x     : in  std_logic;
    found : out std_logic
  );
end seq101;

architecture rtl of seq101 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity seq101 is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    x     : in  std_logic;
    found : out std_logic
  );
end seq101;

architecture rtl of seq101 is
    signal last : std_logic_vector(2 downto 0) := (others => '0');
    signal n    : unsigned(1 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                last <= (others => '0');
                n    <= (others => '0');
            else
                last <= last(1 downto 0) & x;
                if n /= 3 then
                    n <= n + 1;
                end if;
            end if;
        end if;
    end process;
    found <= '1' when last = "101" and n = 3 else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity seq101 is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    x     : in  std_logic;
    found : out std_logic
  );
end seq101;

architecture rtl of seq101 is
    signal last : std_logic_vector(2 downto 0) := (others => '0');
    signal n    : unsigned(1 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                last <= (others => '0');
            else
                last <= last(1 downto 0) & x;
            end if;
        end if;
    end process;
    found <= '1' when last = "110" else '0';
end rtl;
