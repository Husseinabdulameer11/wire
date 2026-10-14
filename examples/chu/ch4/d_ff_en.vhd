-- D flip-flop with asynchronous reset and synchronous
-- enable (Chu, Listings 4.3 and 4.4), written as a
-- register plus next-state logic, the two-segment style.
library ieee;
use ieee.std_logic_1164.all;

entity d_ff_en is
  port(clk, reset : in  std_logic;
       en, d      : in  std_logic;
       q          : out std_logic);
end d_ff_en;

architecture two_seg_arch of d_ff_en is
  signal r_reg, r_next : std_logic;
begin
  -- the D flip-flop
  process(clk, reset)
  begin
    if reset = '1' then
      r_reg <= '0';
    elsif rising_edge(clk) then
      r_reg <= r_next;
    end if;
  end process;
  -- next-state logic: the enable is a 2-to-1 mux
  r_next <= d when en = '1' else r_reg;
  q <= r_reg;
end two_seg_arch;
