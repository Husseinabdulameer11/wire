-- expect: VHDL needs parentheses when and and or are mixed
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_mixed is
  port(a, b, c : in std_logic; y : out std_logic);
end vhdl_mixed;

architecture rtl of vhdl_mixed is
begin
  y <= a and b or c;
end rtl;
