-- Mod-M counter (Chu, Listing 4.11): 0, 1, ..., M-1, 0, ...
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mod_m_counter is
  generic(N : integer := 4;     -- bits
          M : integer := 10);   -- modulus
  port(clk, reset : in  std_logic;
       max_tick   : out std_logic;
       q          : out std_logic_vector(N-1 downto 0));
end mod_m_counter;

architecture arch of mod_m_counter is
  signal r_reg, r_next : unsigned(N-1 downto 0);
begin
  process(clk, reset)
  begin
    if reset = '1' then
      r_reg <= (others => '0');
    elsif rising_edge(clk) then
      r_reg <= r_next;
    end if;
  end process;
  r_next <= (others => '0') when r_reg = M-1 else r_reg + 1;
  q <= std_logic_vector(r_reg);
  max_tick <= '1' when r_reg = M-1 else '0';
end arch;
