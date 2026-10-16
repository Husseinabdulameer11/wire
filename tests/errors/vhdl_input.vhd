-- expect: a is an input of the entity, so it cannot be given a value here
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_input is
  port(a : in std_logic; y : out std_logic);
end vhdl_input;

architecture rtl of vhdl_input is
begin
  a <= '1';
  y <= a;
end rtl;
