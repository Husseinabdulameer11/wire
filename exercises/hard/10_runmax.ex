title: The biggest so far
cycles: 300
random: rst=0.02
---- question
At each rising edge a 4-bit number arrives on `d`. `m` is the **biggest number received** since the last reset: when `d` is bigger than `m`, `m` takes it. `rst` (at a rising edge) sets `m` to 0.
---- hint
Keep the maximum in an `unsigned` signal: `elsif unsigned(d) > mx then mx <= unsigned(d); end if;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity runmax is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    d   : in  std_logic_vector(3 downto 0);
    m   : out std_logic_vector(3 downto 0)
  );
end runmax;

architecture rtl of runmax is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity runmax is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    d   : in  std_logic_vector(3 downto 0);
    m   : out std_logic_vector(3 downto 0)
  );
end runmax;

architecture rtl of runmax is
    signal mx : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                mx <= (others => '0');
            elsif unsigned(d) > mx then
                mx <= unsigned(d);
            end if;
        end if;
    end process;
    m <= std_logic_vector(mx);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity runmax is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    d   : in  std_logic_vector(3 downto 0);
    m   : out std_logic_vector(3 downto 0)
  );
end runmax;

architecture rtl of runmax is
    signal mx : unsigned(3 downto 0) := (others => '0');
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                mx <= (others => '0');
            elsif unsigned(d) > mx then
                mx <= unsigned(d);
            else
                mx <= mx - 1;
            end if;
        end if;
    end process;
    m <= std_logic_vector(mx);
end rtl;
