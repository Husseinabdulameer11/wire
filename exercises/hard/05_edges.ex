title: Counting pulses
cycles: 400
random: rst=0.005 x=0.6
---- question
`n` counts how many times `x` has gone from 0 to 1, as seen at the rising edges of `clk` (it wraps round after 15). Holding `x` at 1 counts once, not at every edge.

Remember `x` from the last edge to see it change. `rst` (at a rising edge) sets the count to 0 and forgets `x`.
---- hint
Keep `prev <= x;` at every edge; count when `x = '1' and prev = '0'`.
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity edgecount is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    x   : in  std_logic;
    n   : out std_logic_vector(3 downto 0)
  );
end edgecount;

architecture rtl of edgecount is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity edgecount is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    x   : in  std_logic;
    n   : out std_logic_vector(3 downto 0)
  );
end edgecount;

architecture rtl of edgecount is
    signal c : unsigned(3 downto 0) := (others => '0');
    signal prev : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                c    <= (others => '0');
                prev <= '0';
            else
                prev <= x;
                if x = '1' and prev = '0' then
                    c <= c + 1;
                end if;
            end if;
        end if;
    end process;
    n <= std_logic_vector(c);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity edgecount is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    x   : in  std_logic;
    n   : out std_logic_vector(3 downto 0)
  );
end edgecount;

architecture rtl of edgecount is
    signal c : unsigned(3 downto 0) := (others => '0');
    signal prev : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                c    <= (others => '0');
                prev <= '0';
            else
                prev <= x;
                if x = '1' then
                    c <= c + 1;
                end if;
            end if;
        end if;
    end process;
    n <= std_logic_vector(c);
end rtl;
