title: A vending machine
cycles: 500
random: rst=0.005
---- question
A drink costs 15. At each rising edge, `coin` says what was put in: `"01"` is a 5, `"10"` is a 10, anything else is nothing.

The machine adds up the money. When the total reaches 15 or more, at that edge it gives the drink: `vend` is 1 for the next cycle, and `change` is 1 too if the total was 20 (it gives 5 back). Then the total starts again from 0. `rst` sets the total to 0, with `vend` and `change` 0.
---- hint
Keep `total` (0, 5 or 10). On each edge, work out `total + coin`; if it is 15 or more, set `vend` (and `change` when it is 20) and set `total` to 0, else store it and clear `vend` and `change`.
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vending is
  port(
    clk    : in  std_logic;
    rst    : in  std_logic;
    coin   : in  std_logic_vector(1 downto 0);
    vend   : out std_logic;
    change : out std_logic
  );
end vending;

architecture rtl of vending is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vending is
  port(
    clk    : in  std_logic;
    rst    : in  std_logic;
    coin   : in  std_logic_vector(1 downto 0);
    vend   : out std_logic;
    change : out std_logic
  );
end vending;

architecture rtl of vending is
    signal total, add, sum : unsigned(4 downto 0) := (others => '0');
    signal v, c : std_logic := '0';
begin
    add <= to_unsigned(5, 5) when coin = "01" else to_unsigned(10, 5) when coin = "10" else to_unsigned(0, 5);
    sum <= total + add;
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                total <= (others => '0');
                v <= '0';
                c <= '0';
            elsif sum >= 15 then
                total <= (others => '0');
                v <= '1';
                if sum = 20 then c <= '1'; else c <= '0'; end if;
            else
                total <= sum;
                v <= '0';
                c <= '0';
            end if;
        end if;
    end process;
    vend   <= v;
    change <= c;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vending is
  port(
    clk    : in  std_logic;
    rst    : in  std_logic;
    coin   : in  std_logic_vector(1 downto 0);
    vend   : out std_logic;
    change : out std_logic
  );
end vending;

architecture rtl of vending is
    signal total, add, sum : unsigned(4 downto 0) := (others => '0');
    signal v, c : std_logic := '0';
begin
    add <= to_unsigned(5, 5) when coin = "01" else to_unsigned(10, 5) when coin = "10" else to_unsigned(0, 5);
    sum <= total + add;
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                total <= (others => '0');
                v <= '0';
                c <= '0';
            elsif sum >= 15 then
                total <= (others => '0');
                v <= '1';
                c <= '0';
            else
                total <= sum;
                v <= '0';
                c <= '0';
            end if;
        end if;
    end process;
    vend   <= v;
    change <= c;
end rtl;
