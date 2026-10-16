-- expect: vhdl_no_library.vhd:15:15: error: std_logic is not visible here: put library ieee; use ieee.std_logic_1164.all; before this entity
library ieee;
use ieee.std_logic_1164.all;

entity inv is
  port(a : in std_logic; y : out std_logic);
end inv;

architecture rtl of inv is
begin
  y <= not a;
end rtl;

entity vhdl_no_library is
  port(a : in std_logic; y : out std_logic);
end vhdl_no_library;

architecture rtl of vhdl_no_library is
begin
  u: entity work.inv port map(a, y);
end rtl;
