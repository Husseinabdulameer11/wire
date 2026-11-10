-- expect: is given a value both on the clock edge and outside it
library ieee;
use ieee.std_logic_1164.all;
entity vhdl_edge_twice is
  port (clk, d : in std_logic; q : out std_logic);
end vhdl_edge_twice;
architecture rtl of vhdl_edge_twice is
begin
  process (all)
  begin
    q <= '0';
    if rising_edge(clk) then
      q <= d;
    end if;
  end process;
end rtl;
