title: A random number generator: the LFSR
cycles: 200
random: rst=0.01
---- question
A **linear feedback shift register** makes a sequence that looks random. This one is 4 bits: at each rising edge it shifts left, and the new bit 0 is bit 3 XOR bit 2. Started from `"0001"`, it goes through all 15 non-zero values before repeating.

`rst` (at a rising edge) loads `"0001"`. `q` shows the register. (It must never be all zeros: it would stay there.)
---- hint
`if rst = '1' then s <= "0001"; else s <= s(2 downto 0) & (s(3) xor s(2)); end if;` and `q <= s;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity lfsr4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end lfsr4;

architecture rtl of lfsr4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity lfsr4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end lfsr4;

architecture rtl of lfsr4 is
    signal s : std_logic_vector(3 downto 0) := "0001";
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                s <= "0001";
            else
                s <= s(2 downto 0) & (s(3) xor s(2));
            end if;
        end if;
    end process;
    q <= s;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity lfsr4 is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end lfsr4;

architecture rtl of lfsr4 is
    signal s : std_logic_vector(3 downto 0) := "0001";
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                s <= "0001";
            else
                s <= s(2 downto 0) & (s(3) xor s(0));
            end if;
        end if;
    end process;
    q <= s;
end rtl;
