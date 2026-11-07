title: An accumulator with overflow
cycles: 800
random: rst=0.002 en=0.9
---- question
An **accumulator** adds up numbers. At each rising edge where `en` is 1, the 4-bit `d` is added to the 8-bit total `acc` (wrapping round past 255).

`ovf` is a **sticky** flag: it becomes 1 at the edge where an addition wraps round, and stays 1 until `rst`. `rst` (at a rising edge) sets the total and `ovf` to 0.
---- hint
Add in 9 bits so you can see the carry: `t <= resize(a, 9) + resize(unsigned(d), 9);` then on an enabled edge `a <= t(7 downto 0);` and `if t(8) = '1' then o <= '1'; end if;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity accum is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    d   : in  std_logic_vector(3 downto 0);
    acc : out std_logic_vector(7 downto 0);
    ovf : out std_logic
  );
end accum;

architecture rtl of accum is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity accum is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    d   : in  std_logic_vector(3 downto 0);
    acc : out std_logic_vector(7 downto 0);
    ovf : out std_logic
  );
end accum;

architecture rtl of accum is
    signal a : unsigned(7 downto 0) := (others => '0');
    signal t : unsigned(8 downto 0);
    signal o : std_logic := '0';
begin
    t <= resize(a, 9) + resize(unsigned(d), 9);
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                a <= (others => '0');
                o <= '0';
            elsif en = '1' then
                a <= t(7 downto 0);
                if t(8) = '1' then
                    o <= '1';
                end if;
            end if;
        end if;
    end process;
    acc <= std_logic_vector(a);
    ovf <= o;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity accum is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    d   : in  std_logic_vector(3 downto 0);
    acc : out std_logic_vector(7 downto 0);
    ovf : out std_logic
  );
end accum;

architecture rtl of accum is
    signal a : unsigned(7 downto 0) := (others => '0');
    signal t : unsigned(8 downto 0);
    signal o : std_logic := '0';
begin
    t <= resize(a, 9) + resize(unsigned(d), 9);
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                a <= (others => '0');
                o <= '0';
            elsif en = '1' then
                a <= t(7 downto 0);
                o <= t(8);
            end if;
        end if;
    end process;
    acc <= std_logic_vector(a);
    ovf <= o;
end rtl;
