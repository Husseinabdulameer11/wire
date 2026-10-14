-- Free-running N-bit binary counter (Chu, Listing 4.9).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity free_run_bin_counter is
  generic(N : integer := 8);
  port(clk, reset : in  std_logic;
       max_tick   : out std_logic;
       q          : out std_logic_vector(N-1 downto 0));
end free_run_bin_counter;

architecture arch of free_run_bin_counter is
  signal r_reg : unsigned(N-1 downto 0);
begin
  process(clk, reset)
  begin
    if reset = '1' then
      r_reg <= (others => '0');
    elsif rising_edge(clk) then
      r_reg <= r_reg + 1;           -- wraps from 2^N-1 to 0
    end if;
  end process;
  q <= std_logic_vector(r_reg);
  max_tick <= '1' when r_reg = 2**N-1 else '0';
end arch;
