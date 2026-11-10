-- division, mod and rem (by constants and by signals, unsigned and signed),
-- abs, powers, integers that can be negative, integer products,
-- to_integer of a signed value, and shifts by wide amounts
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity arith2 is
  port (a, b : in std_logic_vector(7 downto 0);
        sh : in std_logic_vector(6 downto 0);
        n : in std_logic_vector(2 downto 0);
        q, r, sq, sr, sm, q4, m4, ab, sqr, pw, prod, i_out, wide_sh, bitpick : out std_logic_vector(7 downto 0);
        neg : out std_logic);
end arith2;
architecture rtl of arith2 is
  signal ua, ub : unsigned(7 downto 0);
  signal sa, sb : signed(7 downto 0);
  signal ia : integer range -128 to 127;
  signal ib : integer range 0 to 255;
  signal big : unsigned(63 downto 0);
begin
  ua <= unsigned(a);
  ub <= unsigned(b) or x"01";   -- never 0: VHDL stops on a division by 0
  sa <= signed(a);
  sb <= signed(b or x"01");
  q <= std_logic_vector(ua / ub);
  r <= std_logic_vector(ua rem ub);
  sq <= std_logic_vector(sa / sb);
  sr <= std_logic_vector(sa rem sb);
  sm <= std_logic_vector(sa mod sb);
  q4 <= std_logic_vector(ua / 4);
  m4 <= std_logic_vector(resize(ua mod 16, 8));
  ab <= std_logic_vector(abs sa);
  sqr <= std_logic_vector(to_unsigned(to_integer(ua(3 downto 0)) ** 2, 8));
  pw <= std_logic_vector(to_unsigned(2 ** to_integer(unsigned(n)), 8));
  ia <= to_integer(sa);
  ib <= to_integer(ub);
  i_out <= std_logic_vector(to_signed(ia / 2 - 3, 8));
  prod <= std_logic_vector(to_unsigned((ib mod 16) * 13, 8));
  neg <= '1' when ia < -5 else '0';
  big <= shift_left(resize(ua, 64), to_integer(unsigned(sh)));
  wide_sh <= std_logic_vector(big(63 downto 56) xor big(7 downto 0));
  bitpick(0) <= std_logic(big(to_integer(unsigned(sh(5 downto 0)))));
  bitpick(7 downto 1) <= (others => '0');
end rtl;
