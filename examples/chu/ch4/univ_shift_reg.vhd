-- Universal shift register (Chu, Listing 4.8):
-- 00 hold, 01 shift left, 10 shift right, 11 load.
library ieee;
use ieee.std_logic_1164.all;

entity univ_shift_reg is
  generic(N : integer := 8);
  port(clk, reset : in  std_logic;
       ctrl       : in  std_logic_vector(1 downto 0);
       d          : in  std_logic_vector(N-1 downto 0);
       q          : out std_logic_vector(N-1 downto 0));
end univ_shift_reg;

architecture arch of univ_shift_reg is
  signal r_reg, r_next : std_logic_vector(N-1 downto 0);
begin
  process(clk, reset)
  begin
    if reset = '1' then
      r_reg <= (others => '0');
    elsif rising_edge(clk) then
      r_reg <= r_next;
    end if;
  end process;
  -- next-state logic: a 4-to-1 mux
  with ctrl select
    r_next <= r_reg                          when "00",  -- hold
              r_reg(N-2 downto 0) & d(0)     when "01",  -- left
              d(N-1) & r_reg(N-1 downto 1)   when "10",  -- right
              d                              when others; -- load
  q <= r_reg;
end arch;
