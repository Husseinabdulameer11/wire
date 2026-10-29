-- expect: vhdl_wide_here.vhd:15:28: error: this value is 64 bits; values wider than 32 bits can be kept in signals and variables
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_wide_here is
  port(a, b : in  std_logic_vector(31 downto 0);
       i    : in  std_logic_vector(5 downto 0);
       y    : out std_logic);
end vhdl_wide_here;

architecture rtl of vhdl_wide_here is
  signal p : unsigned(63 downto 0);
begin
  y <= '1' when to_integer(p) = 3 else '0';
  p <= unsigned(a) * unsigned(b);
end rtl;
