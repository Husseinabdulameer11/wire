-- expect: vhdl_syntax.vhd:11:14: syntax error
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_syntax is
  port(a : in std_logic; y : out std_logic);
end vhdl_syntax;

architecture rtl of vhdl_syntax is
begin
  y <= a and ;
end rtl;
