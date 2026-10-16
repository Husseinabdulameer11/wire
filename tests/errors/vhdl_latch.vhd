-- expect: vhdl_latch.vhd:12:3: error: q is not given a value on every path through this process
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_latch is
  port(en, d : in  std_logic;
       q     : out std_logic);
end vhdl_latch;

architecture rtl of vhdl_latch is
begin
  process(en, d)
  begin
    if en = '1' then q <= d; end if;
  end process;
end rtl;
