-- Sign-magnitude adder (Chu, Listing 3.14), N = 4:
-- sort by magnitude, then add or subtract magnitudes.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sign_mag_add is
  generic(N : integer := 4);
  port(a, b : in  std_logic_vector(N-1 downto 0);
       sum  : out std_logic_vector(N-1 downto 0));
end sign_mag_add;

architecture arch of sign_mag_add is
  signal mag_a, mag_b, big, small, mag : unsigned(N-2 downto 0);
  signal sign : std_logic;
begin
  mag_a <= unsigned(a(N-2 downto 0));
  mag_b <= unsigned(b(N-2 downto 0));
  -- stage 1: which one is larger?
  big   <= mag_a when mag_a > mag_b else mag_b;
  small <= mag_b when mag_a > mag_b else mag_a;
  sign  <= a(N-1) when mag_a > mag_b else b(N-1);
  -- stage 2: same signs add, different signs subtract
  mag <= big + small when a(N-1) = b(N-1) else big - small;
  sum <= sign & std_logic_vector(mag);
end arch;
