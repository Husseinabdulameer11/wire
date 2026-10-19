-- expect: Wire draws pixels as a screen when it is an array written like a ram
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity vhdl_screen is
  port (clk : in std_logic; a : in std_logic_vector(3 downto 0); y : out std_logic);
end vhdl_screen;
architecture rtl of vhdl_screen is
  signal pixels : std_logic_vector(15 downto 0) := (others => '0');
  attribute wire_screen : integer;
  attribute wire_screen of pixels : signal is 4;
begin
  process (clk)
  begin
    if rising_edge(clk) then
      pixels(to_integer(unsigned(a))) <= '1';
    end if;
  end process;
  y <= pixels(0);
end rtl;
