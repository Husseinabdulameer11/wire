-- Bits: slices of to and downto vectors, aggregates, shifts, literals.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bits is
  port(x    : in  std_logic_vector(7 downto 0);
       r    : in  std_logic_vector(1 to 4);
       e    : in  std_logic;
       sl, sr, ra, rl, rr : out std_logic_vector(7 downto 0);
       agg  : out std_logic_vector(7 downto 0);
       rep  : out std_logic_vector(3 downto 0);
       rev  : out std_logic_vector(3 downto 0);
       mid  : out std_logic_vector(1 to 2);
       k    : out std_logic_vector(11 downto 0);
       nx   : out std_logic_vector(7 downto 0);
       gates : out std_logic_vector(2 downto 0));
end bits;

architecture rtl of bits is
  constant MASK : std_logic_vector(7 downto 0) := x"0F";
  signal u : unsigned(7 downto 0);
  signal s : signed(7 downto 0);
begin
  u  <= unsigned(x);
  s  <= signed(x);
  sl <= std_logic_vector(shift_left(u, 3));
  sr <= std_logic_vector(shift_right(u, 2));
  ra <= std_logic_vector(shift_right(s, 2));
  rl <= x rol 1;
  rr <= std_logic_vector(rotate_right(u, 3));
  agg <= (7 => '1', 0 => e, others => '0');
  rep <= (others => e);
  rev <= r(4) & r(3) & r(2) & r(1);
  mid <= r(2 to 3);
  k   <= x"A" & x(7 downto 4) & "10" & B"0_1";
  nx  <= (x and MASK) or not (x or MASK);
  gates(0) <= x(0) nand x(1);
  gates(1) <= x(2) xnor x(3);
  gates(2) <= not (x(4) nor e);
end rtl;
