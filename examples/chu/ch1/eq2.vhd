-- 1-bit comparator, the building block (Chu, Listing 1.1)
library ieee;
use ieee.std_logic_1164.all;

entity eq1 is
  port(i0, i1 : in  std_logic;
       eq     : out std_logic);
end eq1;

architecture sop_arch of eq1 is
  signal p0, p1 : std_logic;
begin
  p0 <= (not i0) and (not i1);
  p1 <= i0 and i1;
  eq <= p0 or p1;
end sop_arch;

-- 2-bit comparator from two 1-bit ones (Chu, Listing 1.3)
library ieee;
use ieee.std_logic_1164.all;

entity eq2 is
  port(a, b : in  std_logic_vector(1 downto 0);
       aeqb : out std_logic);
end eq2;

architecture struc_arch of eq2 is
  signal e0, e1 : std_logic;
begin
  bit0: entity work.eq1(sop_arch) port map(i0 => a(0), i1 => b(0), eq => e0);
  bit1: entity work.eq1(sop_arch) port map(i0 => a(1), i1 => b(1), eq => e1);
  aeqb <= e0 and e1;     -- equal when both bits are
end struc_arch;
