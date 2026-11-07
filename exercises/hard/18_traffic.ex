title: A timed traffic light
cycles: 300
random: rst=0.01
---- question
A traffic light that stays in each colour for its own number of clock cycles:

| state | lasts | next |
|-------|-------|------|
| red | 4 cycles | green |
| green | 3 cycles | amber |
| amber | 1 cycle | red |

`red`, `green` and `amber` show the current state (exactly one is 1). `rst` (at a rising edge) goes to red, at the start of its 4 cycles.

Keep the state in a signal of your own type, and a timer counting the cycles spent in it.
---- hint
A timer `t`: on each edge, if `t` is the last cycle of the state, move to the next state and set `t` to 0, else `t <= t + 1`. Red's last cycle is `t = 3`, green's `t = 2`, amber's `t = 0`.
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity traffic is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    red   : out std_logic;
    amber : out std_logic;
    green : out std_logic
  );
end traffic;

architecture rtl of traffic is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity traffic is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    red   : out std_logic;
    amber : out std_logic;
    green : out std_logic
  );
end traffic;

architecture rtl of traffic is
    type state_t is (s_red, s_green, s_amber);
    signal st : state_t := s_red;
    signal t  : unsigned(1 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                st <= s_red;
                t  <= (others => '0');
            else
                case st is
                    when s_red =>
                        if t = 3 then st <= s_green; t <= (others => '0'); else t <= t + 1; end if;
                    when s_green =>
                        if t = 2 then st <= s_amber; t <= (others => '0'); else t <= t + 1; end if;
                    when s_amber =>
                        st <= s_red; t <= (others => '0');
                end case;
            end if;
        end if;
    end process;
    red   <= '1' when st = s_red   else '0';
    amber <= '1' when st = s_amber else '0';
    green <= '1' when st = s_green else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity traffic is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    red   : out std_logic;
    amber : out std_logic;
    green : out std_logic
  );
end traffic;

architecture rtl of traffic is
    type state_t is (s_red, s_green, s_amber);
    signal st : state_t := s_red;
    signal t  : unsigned(1 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                st <= s_red;
                t  <= (others => '0');
            else
                case st is
                    when s_red =>
                        if t = 3 then st <= s_green; t <= (others => '0'); else t <= t + 1; end if;
                    when s_green =>
                        if t = 3 then st <= s_amber; t <= (others => '0'); else t <= t + 1; end if;
                    when s_amber =>
                        st <= s_red; t <= (others => '0');
                end case;
            end if;
        end if;
    end process;
    red   <= '1' when st = s_red   else '0';
    amber <= '1' when st = s_amber else '0';
    green <= '1' when st = s_green else '0';
end rtl;
