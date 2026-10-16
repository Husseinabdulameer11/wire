-- expect: vhdl_no_else.vhd:11:10: error: this when has no else
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_no_else is
  port(a, en : in std_logic; y : out std_logic);
end vhdl_no_else;

architecture rtl of vhdl_no_else is
begin
  y <= a when en = '1';
end rtl;
