-- Arithmetic and comparisons, unsigned and signed (numeric_std).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity arith is
  generic(N : integer := 6);
  port(a, b   : in  std_logic_vector(N-1 downto 0);
       ci     : in  std_logic;
       sum    : out std_logic_vector(N downto 0);
       diff   : out std_logic_vector(N-1 downto 0);
       sdiff  : out std_logic_vector(N downto 0);
       neg    : out std_logic_vector(N-1 downto 0);
       plus5  : out std_logic_vector(N-1 downto 0);
       minus3 : out std_logic_vector(N-1 downto 0);
       ult, ule, ugt, uge, ueq, une : out std_logic;
       slt, sge, small : out std_logic;
       withc  : out std_logic_vector(N-1 downto 0));
end arith;

architecture rtl of arith is
  constant TEN : integer := 2 * 5;
  signal ua, ub : unsigned(N-1 downto 0);
  signal sa, sb : signed(N-1 downto 0);
begin
  ua <= unsigned(a); ub <= unsigned(b);
  sa <= signed(a);   sb <= signed(b);
  sum    <= std_logic_vector(('0' & ua) + ('0' & ub));
  diff   <= std_logic_vector(ua - ub);
  sdiff  <= std_logic_vector(resize(sa, N+1) - resize(sb, N+1));
  neg    <= std_logic_vector(-sa);
  plus5  <= std_logic_vector(ua + 5);
  minus3 <= std_logic_vector(ua - 3);
  withc  <= std_logic_vector(ua + ub + ci);
  ult <= '1' when ua < ub  else '0';
  ule <= '1' when ua <= ub else '0';
  ugt <= '1' when ua > ub  else '0';
  uge <= '1' when ua >= ub else '0';
  ueq <= '1' when a = b    else '0';
  une <= '0' when a = b    else '1';
  slt <= '1' when sa < sb  else '0';
  sge <= '1' when sa >= -3 else '0';
  small <= '1' when ua < TEN else '0';
end rtl;
