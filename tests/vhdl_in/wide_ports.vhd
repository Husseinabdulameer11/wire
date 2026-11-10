-- ports wider than 32 bits: at the top and on a chip, given whole and as
-- slices, with a 64 x 64 bit product (128 bits) and a 100-bit value
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity mul64 is
  port (a, b : in std_logic_vector(63 downto 0); p : out std_logic_vector(127 downto 0));
end mul64;
architecture rtl of mul64 is
begin
  p <= std_logic_vector(unsigned(a) * unsigned(b));
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity wide_ports is
  port (clk : in std_logic;
        a, b : in std_logic_vector(63 downto 0);
        p : out std_logic_vector(127 downto 0);
        acc : out std_logic_vector(99 downto 0);
        top8 : out std_logic_vector(7 downto 0));
end wide_ports;
architecture rtl of wide_ports is
  signal prod : std_logic_vector(127 downto 0);
  signal sum : unsigned(99 downto 0) := (others => '0');
begin
  m : entity work.mul64 port map (a => a, b => b, p => prod);
  p <= prod;
  process (clk)
  begin
    if rising_edge(clk) then
      sum <= sum + unsigned(prod(99 downto 0));
    end if;
  end process;
  acc <= std_logic_vector(sum);
  top8 <= prod(127 downto 120);
end rtl;
