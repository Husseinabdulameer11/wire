-- Full adder written as its gates:
-- two half adders (XOR + AND) and an OR.
library ieee;
use ieee.std_logic_1164.all;

entity full_adder is
  port(a, b, cin : in  std_logic;
       s, cout   : out std_logic);
end full_adder;

architecture gates of full_adder is
  signal p, g1, g2 : std_logic;
begin
  p    <= a xor b;        -- half adder 1: sum
  g1   <= a and b;        -- half adder 1: carry
  s    <= p xor cin;      -- half adder 2: sum
  g2   <= p and cin;      -- half adder 2: carry
  cout <= g1 or g2;       -- OR of the two carries
end gates;
