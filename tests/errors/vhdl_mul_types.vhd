-- expect: vhdl_mul_types.vhd:13:37: error: * needs both sides of the same type: this is unsigned, the other side signed
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_mul_types is
  port(a, b : in  std_logic_vector(7 downto 0);
       y    : out std_logic_vector(15 downto 0));
end vhdl_mul_types;

architecture rtl of vhdl_mul_types is
begin
  y <= std_logic_vector(signed(a) * unsigned(b));
end rtl;
