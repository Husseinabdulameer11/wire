-- expect: vhdl_undriven.vhd:7:
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_undriven is
  port(a : in std_logic;
       y, z : out std_logic);
end vhdl_undriven;

architecture rtl of vhdl_undriven is
begin
  y <= not a;
end rtl;
