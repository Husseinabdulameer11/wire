-- 6-bit ring counter: T1..T6, exactly one bit is 1.
-- Six D flip-flops in a loop; CLR presets T1.
library ieee;
use ieee.std_logic_1164.all;

entity ring6 is
  port(clk, clr, en : in  std_logic;
       t : out std_logic_vector(1 to 6));
end ring6;

architecture gates of ring6 is
  signal r : std_logic_vector(1 to 6);
begin
  process(clk, clr)
  begin
    if clr = '1' then
      r <= "100000";                     -- T1 active
    elsif rising_edge(clk) then
      if en = '1' then
        r <= r(6) & r(1 to 5);           -- rotate right
      end if;
    end if;
  end process;
  t <= r;
end gates;
