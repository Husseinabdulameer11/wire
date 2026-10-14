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
-- n-bit register with active-low load (like La', Lb').
-- Each bit is a D flip-flop whose D input comes from a
-- 2-to-1 mux: load ? bus : keep the old value.
library ieee;
use ieee.std_logic_1164.all;

entity reg_n is
  generic(N : natural := 8);
  port(clk, clr : in  std_logic;
       en     : in  std_logic;   -- clock enable
       load_n : in  std_logic;
       d      : in  std_logic_vector(N-1 downto 0);
       q      : out std_logic_vector(N-1 downto 0));
end reg_n;

architecture gates of reg_n is
  signal q_reg, q_next : std_logic_vector(N-1 downto 0);
  signal ld : std_logic;
begin
  ld <= (not load_n) and en;
  -- the mux, one AND-OR per bit, written as gates
  mux: for i in 0 to N-1 generate
    q_next(i) <= (ld and d(i))
              or ((not ld) and q_reg(i));
  end generate;
  -- the D flip-flops
  process(clk, clr)
  begin
    if clr = '1' then
      q_reg <= (others => '0');
    elsif rising_edge(clk) then
      q_reg <= q_next;
    end if;
  end process;
  q <= q_reg;
end gates;
-- 4-bit program counter, synchronous binary counter.
-- Bit i toggles when cp='1' and all lower bits are 1:
--   t(i) = cp and q(0) and ... and q(i-1)
--   q+   = q xor t
library ieee;
use ieee.std_logic_1164.all;

entity pc4 is
  port(clk, clr, en : in  std_logic;
       cp : in  std_logic;               -- count
       q  : out std_logic_vector(3 downto 0));
end pc4;

architecture gates of pc4 is
  signal q_reg, t : std_logic_vector(3 downto 0);
begin
  t(0) <= cp and en;
  t(1) <= t(0) and q_reg(0);           -- AND chain
  t(2) <= t(1) and q_reg(1);
  t(3) <= t(2) and q_reg(2);
  process(clk, clr)
  begin
    if clr = '1' then
      q_reg <= (others => '0');
    elsif rising_edge(clk) then
      q_reg <= q_reg xor t;              -- T flip-flops
    end if;
  end process;
  q <= q_reg;
end gates;
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
-- 16 x 8 RAM, program preloaded (the "switches").
-- Read is combinational, like the 74189 chip:
-- address in, data out after the access time.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ram16x8 is
  port(addr : in  std_logic_vector(3 downto 0);
       dout : out std_logic_vector(7 downto 0));
end ram16x8;

architecture rtl of ram16x8 is
  type ram_t is array (0 to 15) of
       std_logic_vector(7 downto 0);
  constant RAM : ram_t := (
    0  => x"09",  -- LDA 9H
    1  => x"1A",  -- ADD AH
    2  => x"1B",  -- ADD BH
    3  => x"2C",  -- SUB CH
    4  => x"E0",  -- OUT
    5  => x"F0",  -- HLT
    9  => x"10",  -- 16
    10 => x"14",  -- 20
    11 => x"18",  -- 24
    12 => x"20",  -- 32
    others => x"00");
begin
  dout <= RAM(to_integer(unsigned(addr)));
end rtl;
-- SAP-1 top level: wires the blocks to one 8-bit W bus.
-- The bus is an AND-OR multiplexer: each source is
-- ANDed with its enable and the results are ORed.
-- (It replaces the tri-state bus of the chip version.)
library ieee;
use ieee.std_logic_1164.all;

entity sap1 is
  port(clk, clr : in  std_logic;
       outp     : out std_logic_vector(7 downto 0);
       halted   : out std_logic);
end sap1;

architecture structural of sap1 is
  signal w : std_logic_vector(7 downto 0);  -- W bus
  signal pc, mar : std_logic_vector(3 downto 0);
  signal ram_q, ir, a, b, alu :
         std_logic_vector(7 downto 0);
  signal t : std_logic_vector(1 to 6);
  signal cp, ep, lm_n, ce_n, li_n, ei_n, la_n, ea,
         su, eu, lb_n, lo_n, hlt : std_logic;
  signal run, halt_reg : std_logic;

  -- repeat one enable bit 8 times, for AND-ing a byte
  function x8(e : std_logic) return std_logic_vector is
  begin
    return (7 downto 0 => e);
  end function;
begin
  -- HLT: stop with a clock ENABLE, never gate the clock
  process(clk, clr)
  begin
    if clr = '1' then
      halt_reg <= '0';
    elsif rising_edge(clk) then
      if t(4) = '1' and hlt = '1' then
        halt_reg <= '1';
      end if;
    end if;
  end process;
  run    <= not (halt_reg or (t(4) and hlt));
  halted <= halt_reg;

  -- the W bus: AND-OR mux of all bus sources
  w <= (x8(ep)       and ("0000" & pc))
    or (x8(not ce_n) and ram_q)
    or (x8(not ei_n) and ("0000" & ir(3 downto 0)))
    or (x8(ea)       and a)
    or (x8(eu)       and alu);

  u_pc : entity work.pc4
    port map(clk, clr, run, cp, pc);
  u_mar: entity work.reg_n generic map(4)
    port map(clk, clr, run, lm_n, w(3 downto 0), mar);
  u_ram: entity work.ram16x8
    port map(mar, ram_q);
  u_ir : entity work.reg_n generic map(8)
    port map(clk, clr, run, li_n, w, ir);
  u_a  : entity work.reg_n generic map(8)
    port map(clk, clr, run, la_n, w, a);
  u_b  : entity work.reg_n generic map(8)
    port map(clk, clr, run, lb_n, w, b);
  u_alu: entity work.addsub8
    port map(a, b, su, alu);
  u_out: entity work.reg_n generic map(8)
    port map(clk, clr, run, lo_n, w, outp);
  u_rc : entity work.ring6
    port map(clk, clr, run, t);
  u_ctl: entity work.control
    port map(ir(7 downto 4), t, hlt, cp, ep, lm_n, ce_n,
             li_n, ei_n, la_n, ea, su, eu, lb_n, lo_n);
end structural;
