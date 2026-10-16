-- expect: the target is std_logic_vector but the value is unsigned: convert it with std_logic_vector(...)
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_type is
  port(a, b : in std_logic_vector(3 downto 0); y : out std_logic_vector(3 downto 0));
end vhdl_type;

architecture rtl of vhdl_type is
begin
  y <= unsigned(a) + unsigned(b);
end rtl;
