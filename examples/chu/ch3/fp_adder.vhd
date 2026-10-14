-- Simplified floating-point adder (Chu, Listing 3.19).
-- sign, 4-bit exponent, 8-bit normalized fraction;
-- sort, align, add/subtract, normalize. No rounding.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity fp_adder is
  port(sign1, sign2 : in  std_logic;
       exp1, exp2   : in  std_logic_vector(3 downto 0);
       frac1, frac2 : in  std_logic_vector(7 downto 0);
       sign_out     : out std_logic;
       exp_out      : out std_logic_vector(3 downto 0);
       frac_out     : out std_logic_vector(7 downto 0));
end fp_adder;

architecture arch of fp_adder is
  -- b = big, s = small, a = aligned, n = normalized
  signal signb, signs : std_logic;
  signal expb, exps, diff, expn : unsigned(3 downto 0);
  signal fracb, fracs, fraca, fracn, norm : unsigned(7 downto 0);
  signal sum   : unsigned(8 downto 0);     -- one more bit for the carry
  signal lead0 : unsigned(2 downto 0);
  signal big   : boolean;
begin
  -- 1: sort, compare exponent and fraction together
  big   <= unsigned(exp1 & frac1) > unsigned(exp2 & frac2);
  signb <= sign1 when big else sign2;
  signs <= sign2 when big else sign1;
  expb  <= unsigned(exp1)  when big else unsigned(exp2);
  exps  <= unsigned(exp2)  when big else unsigned(exp1);
  fracb <= unsigned(frac1) when big else unsigned(frac2);
  fracs <= unsigned(frac2) when big else unsigned(frac1);

  -- 2: align the smaller fraction
  diff  <= expb - exps;
  fraca <= shift_right(fracs, to_integer(diff)) when diff < 8
           else (others => '0');

  -- 3: add or subtract
  sum <= ('0' & fracb) + ('0' & fraca) when signb = signs else
         ('0' & fracb) - ('0' & fraca);

  -- 4: normalize; count the zeros before the first 1
  process(sum)
    variable n : integer range 0 to 7;
  begin
    n := 7;
    for i in 7 downto 1 loop
      if sum(i) = '1' then
        n := 7 - i;
        exit;
      end if;
    end loop;
    lead0 <= to_unsigned(n, 3);
  end process;
  norm <= shift_left(sum(7 downto 0), to_integer(lead0));

  process(sum, norm, expb, lead0)
  begin
    if sum(8) = '1' then              -- carry: shift right once
      expn  <= expb + 1;
      fracn <= sum(8 downto 1);
    elsif lead0 > expb then           -- too small: call it 0
      expn  <= (others => '0');
      fracn <= (others => '0');
    else
      expn  <= expb - lead0;
      fracn <= norm;
    end if;
  end process;

  sign_out <= signb;
  exp_out  <= std_logic_vector(expn);
  frac_out <= std_logic_vector(fracn);
end arch;
