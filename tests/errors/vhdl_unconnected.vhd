-- expect: input b of half_adder is not connected (and has no default value)
library ieee;
use ieee.std_logic_1164.all;

entity half_adder is
  port(a, b : in std_logic; s, c : out std_logic);
end half_adder;

architecture rtl of half_adder is
begin
  s <= a xor b;
  c <= a and b;
end rtl;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_unconnected is
  port(x : in std_logic; y : out std_logic);
end vhdl_unconnected;

architecture rtl of vhdl_unconnected is
begin
  h: entity work.half_adder port map(a => x, s => y);
end rtl;
