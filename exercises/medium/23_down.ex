title: Counting down
cycles: 200
random: rst=0.03
---- question
A 4-bit counter that counts **down** by one at every rising edge: 15, 14 ... 0, then round to 15. `rst` sets it to 15.
---- hint
An `unsigned` signal: `if rst = '1' then c <= (others => '1'); else c <= c - 1; end if;` and `q <= std_logic_vector(c);`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity down4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end down4;

architecture rtl of down4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity down4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end down4;

architecture rtl of down4 is
    signal c : unsigned(3 downto 0) := (others => '1');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                c <= (others => '1');
            else
                c <= c - 1;
            end if;
        end if;
    end process;
    q <= std_logic_vector(c);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity down4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end down4;

architecture rtl of down4 is
    signal c : unsigned(3 downto 0) := (others => '1');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                c <= (others => '0');
            else
                c <= c - 1;
            end if;
        end if;
    end process;
    q <= std_logic_vector(c);
end rtl;
