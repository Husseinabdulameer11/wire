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
-- 8-bit adder-subtractor:
--   su='0': s = A + B      su='1': s = A - B
-- XOR gates invert B when su='1', and su is also the
-- carry in, so A + not(B) + 1 = A - B.
library ieee;
use ieee.std_logic_1164.all;

entity addsub8 is
  port(a, b : in  std_logic_vector(7 downto 0);
       su   : in  std_logic;
       s    : out std_logic_vector(7 downto 0));
end addsub8;

architecture ripple of addsub8 is
  signal bx : std_logic_vector(7 downto 0);
  signal c  : std_logic_vector(8 downto 0);
begin
  c(0) <= su;
  stage: for i in 0 to 7 generate
    bx(i) <= b(i) xor su;     -- controlled inverter
    fa: entity work.full_adder
      port map(a => a(i), b => bx(i), cin => c(i),
               s => s(i), cout => c(i+1));
  end generate;
  -- c(8) = carry out; SAP-1 ignores it (no flags)
end ripple;
