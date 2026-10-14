-- Free-running shift-right register (Chu, Listing 4.7).
library ieee;
use ieee.std_logic_1164.all;

entity free_run_shift_reg is
  generic(N : integer := 8);
  port(clk, reset : in  std_logic;
       s_in       : in  std_logic;
       s_out      : out std_logic);
end free_run_shift_reg;

architecture arch of free_run_shift_reg is
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
  r_next <= s_in & r_reg(N-1 downto 1);   -- in at the top
  s_out  <= r_reg(0);                     -- out at the bottom
end arch;
