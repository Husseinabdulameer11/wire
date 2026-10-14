-- ROM with synchronous read (Chu, Listing 11.6): the
-- address is registered, so a block RAM can hold it.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity xilinx_rom_sync_template is
  port(clk  : in  std_logic;
       addr : in  std_logic_vector(3 downto 0);
       data : out std_logic_vector(6 downto 0));
end xilinx_rom_sync_template;

architecture arch of xilinx_rom_sync_template is
  type rom_type is array (0 to 15) of std_logic_vector(6 downto 0);
  constant HEX2LED_ROM : rom_type := (
    "0000001", "1001111", "0010010", "0000110",
    "1001100", "0100100", "0100000", "0001111",
    "0000000", "0000100", "0001000", "1100000",
    "0110001", "1000010", "0110000", "0111000");
  -- no reset; the start value is for the simulator
  signal addr_reg : std_logic_vector(3 downto 0) := (others => '0');
begin
  process(clk)
  begin
    if rising_edge(clk) then
      addr_reg <= addr;
    end if;
  end process;
  data <= HEX2LED_ROM(to_integer(unsigned(addr_reg)));
end arch;
