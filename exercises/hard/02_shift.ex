title: A shift register
cycles: 300
random: rst=0.02 en=0.7
---- question
A **shift register** moves its bits along by one place on every clock edge. Bits go in one at a time on `si` (serial in) and come out all at once on `q` (8 bits).

- `rst` = 1 at an edge: all bits 0
- `en` = 1 at an edge: shift left by one, with `si` coming in as the new `q(0)`: the old `q(6 downto 0)` moves up to `q(7 downto 1)`, and the old `q(7)` is lost
- `en` = 0: keep the bits

Receivers of serial links, like a UART, are built on one of these.
---- hint
Keep the bits in a signal `r`, and shift with `r <= r(6 downto 0) & si;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity shift8 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    si  : in  std_logic;
    q   : out std_logic_vector(7 downto 0)
  );
end shift8;

architecture rtl of shift8 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity shift8 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    si  : in  std_logic;
    q   : out std_logic_vector(7 downto 0)
  );
end shift8;

architecture rtl of shift8 is
    signal r : std_logic_vector(7 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                r <= (others => '0');
            elsif en = '1' then
                r <= r(6 downto 0) & si;
            end if;
        end if;
    end process;
    q <= r;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity shift8 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    si  : in  std_logic;
    q   : out std_logic_vector(7 downto 0)
  );
end shift8;

architecture rtl of shift8 is
    signal r : std_logic_vector(7 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                r <= (others => '0');
            elsif en = '1' then
                r <= si & r(7 downto 1);
            end if;
        end if;
    end process;
    q <= r;
end rtl;
