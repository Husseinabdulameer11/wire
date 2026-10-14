-- Period counter, in ms (Chu, Listing 6.7). Chu counts
-- 50_000 clocks a ms (50 MHz); 4 here.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity period_counter is
  port(clk, reset : in  std_logic;
       start, si  : in  std_logic;
       ready, done_tick : out std_logic;
       prd        : out std_logic_vector(9 downto 0));
end period_counter;

architecture arch of period_counter is
  constant CLK_MS_COUNT : integer := 4;
  type state_type is (idle, waite, count, done);
  signal state_reg, state_next : state_type;
  signal t_reg, t_next : unsigned(1 downto 0);
  signal p_reg, p_next : unsigned(9 downto 0);
  signal delay_reg, edge : std_logic;
begin
  process(clk, reset)
  begin
    if reset = '1' then
      state_reg <= idle;
      t_reg <= (others => '0');
      p_reg <= (others => '0');
      delay_reg <= '0';
    elsif rising_edge(clk) then
      state_reg <= state_next;
      t_reg <= t_next;
      p_reg <= p_next;
      delay_reg <= si;
    end if;
  end process;
  edge <= (not delay_reg) and si;      -- rising edge of si

  process(all)
  begin
    ready <= '0';
    done_tick <= '0';
    state_next <= state_reg;
    t_next <= t_reg;
    p_next <= p_reg;
    case state_reg is
      when idle =>
        ready <= '1';
        if start = '1' then
          state_next <= waite;
        end if;
      when waite =>                    -- for the first edge
        if edge = '1' then
          state_next <= count;
          t_next <= (others => '0');
          p_next <= (others => '0');
        end if;
      when count =>
        if edge = '1' then             -- the second edge
          state_next <= done;
        elsif t_reg = CLK_MS_COUNT - 1 then   -- a ms
          t_next <= (others => '0');
          p_next <= p_reg + 1;
        else
          t_next <= t_reg + 1;
        end if;
      when done =>
        done_tick <= '1';
        state_next <= idle;
    end case;
  end process;
  prd <= std_logic_vector(p_reg);
end arch;
