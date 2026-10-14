-- Controller: instruction decoder + control matrix.
-- Pure gates: AND gates decode the opcode, AND-OR
-- networks combine T-states and instructions.
library ieee;
use ieee.std_logic_1164.all;

entity control is
  port(op : in  std_logic_vector(3 downto 0);
       t  : in  std_logic_vector(1 to 6);
       hlt : out std_logic;
       cp, ep, lm_n, ce_n, li_n, ei_n,
       la_n, ea, su, eu, lb_n, lo_n : out std_logic);
end control;

architecture gates of control is
  signal n : std_logic_vector(3 downto 0);
  signal lda, add, sub, outp, mem, ari : std_logic;
begin
  -- instruction decoder (one AND gate per opcode)
  n <= not op;         -- the inverted rails
  lda  <= n(3) and n(2)  and n(1)  and n(0);  -- 0000
  add  <= n(3) and n(2)  and n(1)  and op(0); -- 0001
  sub  <= n(3) and n(2)  and op(1) and n(0);  -- 0010
  outp <= op(3) and op(2) and op(1) and n(0); -- 1110
  hlt  <= op(3) and op(2) and op(1) and op(0);-- 1111
  mem  <= lda or add or sub;      -- uses RAM
  ari  <= add or sub;             -- uses B and the ALU

  -- control matrix (Malvino's equations)
  cp   <= t(2);
  ep   <= t(1);
  lm_n <= not (t(1) or (t(4) and mem));
  ce_n <= not (t(3) or (t(5) and mem));
  li_n <= not t(3);
  ei_n <= not (t(4) and mem);
  la_n <= not ((t(5) and lda) or (t(6) and ari));
  ea   <= t(4) and outp;
  su   <= t(6) and sub;
  eu   <= t(6) and ari;
  lb_n <= not (t(5) and ari);
  lo_n <= not (t(4) and outp);
end gates;
