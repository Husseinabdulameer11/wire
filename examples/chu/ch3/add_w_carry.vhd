-- N-bit adder with a carry out (Chu, Listings 3.9-3.11).
-- Widen both operands by one bit; the extra bit of the
-- sum is the carry. N is a generic, 4 by default.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity add_w_carry is
  generic(N : integer := 4);
  port(a, b : in  std_logic_vector(N-1 downto 0);
       cout : out std_logic;
       sum  : out std_logic_vector(N-1 downto 0));
end add_w_carry;

architecture arch of add_w_carry is
  signal wide : unsigned(N downto 0);
begin
  wide <= unsigned('0' & a) + unsigned('0' & b);
  sum  <= std_logic_vector(wide(N-1 downto 0));
  cout <= wide(N);                   -- the extra bit
end arch;
