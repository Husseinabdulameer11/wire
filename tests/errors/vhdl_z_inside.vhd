-- expect: 'Z' (nothing driving) can only be the whole value given to a signal
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_z_inside is
  port (a : in std_logic; y : out std_logic);
end vhdl_z_inside;
architecture rtl of vhdl_z_inside is
begin
  y <= a and 'Z';
end rtl;
