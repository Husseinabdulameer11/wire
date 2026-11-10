-- expect: one process has one clock
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_two_clocks is
  port (c1, c2, d : in std_logic; q, r : out std_logic);
end vhdl_two_clocks;
architecture rtl of vhdl_two_clocks is
begin
  process (c1, c2)
  begin
    if rising_edge(c1) then q <= d; end if;
    if rising_edge(c2) then r <= d; end if;
  end process;
end rtl;
