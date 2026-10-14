-- ROM with asynchronous read (Chu, Listing 11.5):
-- a constant array, indexed by the address.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rom_template is
  port(addr : in  std_logic_vector(3 downto 0);
       data : out std_logic_vector(6 downto 0));
end rom_template;

architecture arch of rom_template is
  type rom_type is array (0 to 15) of std_logic_vector(6 downto 0);
  -- hex digit to segments a..g, active low
  constant HEX2LED_ROM : rom_type := (
    "0000001", "1001111", "0010010", "0000110",   -- 0 1 2 3
    "1001100", "0100100", "0100000", "0001111",   -- 4 5 6 7
    "0000000", "0000100", "0001000", "1100000",   -- 8 9 A b
    "0110001", "1000010", "0110000", "0111000");  -- C d E F
begin
  data <= HEX2LED_ROM(to_integer(unsigned(addr)));
end arch;
