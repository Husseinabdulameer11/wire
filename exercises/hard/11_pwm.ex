title: Dimming an LED: PWM
cycles: 400
random: rst=0.005
---- question
**Pulse-width modulation** dims an LED by switching it on for part of each period. A 4-bit counter counts 0 to 15 and round again at every rising edge. `pwm` is 1 while the counter is **less than** the duty `d`: `d = 0` is always off, `d = 8` is on half the time.

`rst` (at a rising edge) sets the counter to 0.
---- hint
A free-running counter `c`, and `pwm <= '1' when c < unsigned(d) else '0';`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pwm4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    d   : in  std_logic_vector(3 downto 0);
    pwm : out std_logic
  );
end pwm4;

architecture rtl of pwm4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pwm4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    d   : in  std_logic_vector(3 downto 0);
    pwm : out std_logic
  );
end pwm4;

architecture rtl of pwm4 is
    signal c : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                c <= (others => '0');
            else
                c <= c + 1;
            end if;
        end if;
    end process;
    pwm <= '1' when c < unsigned(d) else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pwm4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    d   : in  std_logic_vector(3 downto 0);
    pwm : out std_logic
  );
end pwm4;

architecture rtl of pwm4 is
    signal c : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                c <= (others => '0');
            else
                c <= c + 1;
            end if;
        end if;
    end process;
    pwm <= '1' when c <= unsigned(d) else '0';
end rtl;
