-- expect: vhdl_func_return.vhd:20:8: error: pick does not return a value on every path
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_func_return is
  port(a, b, s : in  std_logic;
       y       : out std_logic);
end vhdl_func_return;

architecture rtl of vhdl_func_return is
  function pick(x, z, c : std_logic) return std_logic is
  begin
    if c = '1' then
      return z;
    end if;
  end function;
begin
  -- a function must return on every path
  -- (here it does not when c is '0')
  y <= pick(a, b, s);
end rtl;
