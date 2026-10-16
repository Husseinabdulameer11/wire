-- expect: vhdl_width.vhd:12:8: error: the target is 4 bits, but the value is 8 bits
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_width is
  port(a, b : in  std_logic_vector(3 downto 0);
       y    : out std_logic_vector(3 downto 0));
end vhdl_width;

architecture rtl of vhdl_width is
begin
  y <= a & b;
end rtl;
