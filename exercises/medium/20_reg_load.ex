title: A register with load and reset
cycles: 200
random: rst=0.1 load=0.3
---- question
A 4-bit register. At each rising edge of `clk`:

- `rst` = 1: `q` becomes 0
- else, `load` = 1: `q` takes `d`
- else `q` keeps its value

`rst` wins over `load`.
---- hint
Inside `if rising_edge(clk)`: `if rst = '1' then q <= (others => '0'); elsif load = '1' then q <= d; end if;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity reg4 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    load : in  std_logic;
    d    : in  std_logic_vector(3 downto 0);
    q    : out std_logic_vector(3 downto 0)
  );
end reg4;

architecture rtl of reg4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity reg4 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    load : in  std_logic;
    d    : in  std_logic_vector(3 downto 0);
    q    : out std_logic_vector(3 downto 0)
  );
end reg4;

architecture rtl of reg4 is
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                q <= (others => '0');
            elsif load = '1' then
                q <= d;
            end if;
        end if;
    end process;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity reg4 is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    load : in  std_logic;
    d    : in  std_logic_vector(3 downto 0);
    q    : out std_logic_vector(3 downto 0)
  );
end reg4;

architecture rtl of reg4 is
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if load = '1' then
                q <= d;
            elsif rst = '1' then
                q <= (others => '0');
            end if;
        end if;
    end process;
end rtl;
