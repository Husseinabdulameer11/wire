-- expect: an else or elsif after the clock edge is not hardware
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_else_edge is
  port (clk, d : in std_logic; q, r : out std_logic);
end vhdl_else_edge;
architecture rtl of vhdl_else_edge is
begin
  process (clk)
  begin
    if rising_edge(clk) then
      q <= d;
    else
      r <= d;
    end if;
  end process;
end rtl;
