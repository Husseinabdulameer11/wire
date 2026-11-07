title: A T flip-flop
cycles: 200
random: rst=0.05 t=0.5
---- question
A **T flip-flop** flips its output at a rising edge when `t` is 1, and keeps it when `t` is 0. `rst` (at a rising edge) sets it to 0.

An output cannot be read inside the architecture, so keep the value in a signal and connect `q` to it.
---- hint
A signal `s`: on the edge, `if rst = '1' then s <= '0'; elsif t = '1' then s <= not s; end if;` and outside, `q <= s;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tff is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    t   : in  std_logic;
    q   : out std_logic
  );
end tff;

architecture rtl of tff is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tff is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    t   : in  std_logic;
    q   : out std_logic
  );
end tff;

architecture rtl of tff is
    signal s : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                s <= '0';
            elsif t = '1' then
                s <= not s;
            end if;
        end if;
    end process;
    q <= s;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tff is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    t   : in  std_logic;
    q   : out std_logic
  );
end tff;

architecture rtl of tff is
    signal s : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                s <= '0';
            else
                s <= not s;
            end if;
        end if;
    end process;
    q <= s;
end rtl;
