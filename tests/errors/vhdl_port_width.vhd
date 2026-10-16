-- expect: vhdl_port_width.vhd:24:19: error: port x of inv4 is 4 bits, but this is 8 bits
library ieee;
use ieee.std_logic_1164.all;

entity inv4 is
  port(x : in std_logic_vector(3 downto 0); y : out std_logic_vector(3 downto 0));
end inv4;

architecture rtl of inv4 is
begin
  y <= not x;
end rtl;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_port_width is
  port(a : in std_logic_vector(7 downto 0); y : out std_logic_vector(3 downto 0));
end vhdl_port_width;

architecture rtl of vhdl_port_width is
begin
  u1: entity work.inv4
    port map(x => a, y => y);
end rtl;
