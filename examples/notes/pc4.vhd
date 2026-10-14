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
