-- expect: wire_cpu: nothing is not declared
library ieee;
use ieee.std_logic_1164.all;

entity cpu_vhdl_text is
  port(clk : in std_logic; q : out std_logic);
end cpu_vhdl_text;

architecture rtl of cpu_vhdl_text is
  attribute wire_cpu : string;
  attribute wire_cpu of rtl : architecture is "isa=tiny8.isa; text=nothing";
begin
  q <= clk;
end rtl;
