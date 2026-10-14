-- D, T and JK flip-flops: one process, rising edge,
-- asynchronous clear. T and JK are written as what
-- goes into a D flip-flop (next-state logic).
library ieee;
use ieee.std_logic_1164.all;

entity flip_flops is
  port(clk, clr   : in  std_logic;
       d, t, j, k : in  std_logic;
       q_d, q_t, q_jk : out std_logic);
end flip_flops;

architecture rtl of flip_flops is
  signal rd, rt, rjk : std_logic;
begin
  process(clk, clr)
  begin
    if clr = '1' then
      rd <= '0'; rt <= '0'; rjk <= '0';
    elsif rising_edge(clk) then
      rd  <= d;
      rt  <= rt xor t;                              -- toggle
      rjk <= (j and not rjk) or (not k and rjk);    -- JK
    end if;
  end process;
  q_d <= rd; q_t <= rt; q_jk <= rjk;
end rtl;
