-- Single-port RAM with asynchronous read (Chu, Listing
-- 11.1), 16 x 8 here. The starting value of the array is
-- only for the simulator (Wire's RAM starts at 0).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity xilinx_one_port_ram_async is
  generic(ADDR_WIDTH : integer := 4;
          DATA_WIDTH : integer := 8);
  port(clk  : in  std_logic;
       we   : in  std_logic;
       addr : in  std_logic_vector(ADDR_WIDTH-1 downto 0);
       din  : in  std_logic_vector(DATA_WIDTH-1 downto 0);
       dout : out std_logic_vector(DATA_WIDTH-1 downto 0));
end xilinx_one_port_ram_async;

architecture beh_arch of xilinx_one_port_ram_async is
  type ram_type is array (0 to 2**ADDR_WIDTH-1) of
       std_logic_vector(DATA_WIDTH-1 downto 0);
  signal ram : ram_type := (others => (others => '0'));
begin
  process(clk)                 -- write: at the clock edge
  begin
    if rising_edge(clk) then
      if we = '1' then
        ram(to_integer(unsigned(addr))) <= din;
      end if;
    end if;
  end process;
  dout <= ram(to_integer(unsigned(addr)));   -- read: at once
end beh_arch;
