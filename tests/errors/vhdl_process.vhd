-- expect: vhdl_process.vhd:12:3: error: processes are not supported yet
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_process is
  port(clk, d : in  std_logic;
       q      : out std_logic);
end vhdl_process;

architecture rtl of vhdl_process is
begin
  process(clk)
  begin
    if rising_edge(clk) then q <= d; end if;
  end process;
end rtl;
