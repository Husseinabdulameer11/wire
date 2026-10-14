-- BCD to 7-segment decoder as sums of products
-- (from Karnaugh maps, with 10..15 as don't-cares).
-- disp(0) = segment a ... disp(6) = g, '1' = lit.
library ieee;
use ieee.std_logic_1164.all;

entity bcd7 is
  port(v    : in  std_logic_vector(3 downto 0);
       disp : out std_logic_vector(6 downto 0));
end bcd7;

architecture sop of bcd7 is
  signal a, b, c, d : std_logic;
begin
  a <= v(3); b <= v(2); c <= v(1); d <= v(0);
  disp(0) <= a or c or (b and d) or (not b and not d);
  disp(1) <= not b or (not c and not d) or (c and d);
  disp(2) <= b or not c or d;
  disp(3) <= a or (not b and not d) or (not b and c) or (c and not d)
             or (b and not c and d);
  disp(4) <= (not b and not d) or (c and not d);
  disp(5) <= a or (not c and not d) or (b and not c) or (b and not d);
  disp(6) <= a or (not b and c) or (b and not c) or (c and not d);
end sop;
