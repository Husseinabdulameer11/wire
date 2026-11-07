title: Detecting a rising edge
cycles: 300
random: rst=0.02 level=0.5
---- question
A button or a slow signal stays 1 for many clock cycles, but often you want to act **once**, when it changes from 0 to 1. That is an **edge detector**.

`tick` must be 1 for exactly one clock cycle: the cycle after the clock edge at which `level` was 1 while at the edge before it was 0. Both `tick` and the remembered old value change only on clock edges (they are registers), and `rst` clears both.

Remember the value of `level` from the last edge in a signal, and compare.
---- hint
In the clocked process: `prev <= level; if level = '1' and prev = '0' then tick_r <= '1'; else tick_r <= '0'; end if;` Then `tick <= tick_r;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity edge_detect is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    level : in  std_logic;
    tick  : out std_logic
  );
end edge_detect;

architecture rtl of edge_detect is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity edge_detect is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    level : in  std_logic;
    tick  : out std_logic
  );
end edge_detect;

architecture rtl of edge_detect is
    signal prev, tick_r : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                prev   <= '0';
                tick_r <= '0';
            else
                prev <= level;
                if level = '1' and prev = '0' then
                    tick_r <= '1';
                else
                    tick_r <= '0';
                end if;
            end if;
        end if;
    end process;
    tick <= tick_r;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity edge_detect is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    level : in  std_logic;
    tick  : out std_logic
  );
end edge_detect;

architecture rtl of edge_detect is
    signal prev, tick_r : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                prev   <= '0';
                tick_r <= '0';
            else
                prev   <= level;
                tick_r <= level;
            end if;
        end if;
    end process;
    tick <= tick_r;
end rtl;
