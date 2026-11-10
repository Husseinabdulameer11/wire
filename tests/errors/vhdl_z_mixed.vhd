-- expect: has some bits 'Z' and some not
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_z_mixed is
  port (oe : in std_logic; y : out std_logic_vector(3 downto 0));
end vhdl_z_mixed;
architecture rtl of vhdl_z_mixed is
begin
  y <= "ZZ01" when oe = '0' else "1111";
end rtl;
