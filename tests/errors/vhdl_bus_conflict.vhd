-- expect: d is given values by several statements, but this one always drives it
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_bus_conflict is
  port (a, b, oe : in std_logic; y : out std_logic);
end vhdl_bus_conflict;
architecture rtl of vhdl_bus_conflict is
  signal d : std_logic;
begin
  d <= a when oe = '1' else 'Z';
  d <= not b;
  y <= d;
end rtl;
