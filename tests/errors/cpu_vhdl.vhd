-- expect: wire_cpu has no setting called bogus
library ieee;
use ieee.std_logic_1164.all;

entity cpu_vhdl is
  port(clk : in std_logic; q : out std_logic);
end cpu_vhdl;

architecture rtl of cpu_vhdl is
  attribute wire_cpu : string;
  attribute wire_cpu of rtl : architecture is "isa=tiny8.isa; text=q; bogus=1";
begin
  q <= clk;
end rtl;
