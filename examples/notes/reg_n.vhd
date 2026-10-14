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
