-- SR latch: two cross-coupled NOR gates. The loop is
-- the memory. Signals need a starting value, or the
-- simulator starts the loop at 'U' (a real latch
-- powers up either way).
library ieee;
use ieee.std_logic_1164.all;

entity sr_latch is
  port(s, r  : in  std_logic;
       q, nq : out std_logic);
end sr_latch;

architecture gates of sr_latch is
  signal q_i  : std_logic := '0';
  signal nq_i : std_logic := '1';
begin
  q_i  <= r nor nq_i;
  nq_i <= s nor q_i;
  q  <= q_i;
  nq <= nq_i;
end gates;
