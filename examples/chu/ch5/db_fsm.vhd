-- Debouncing FSM (Chu, Listing 5.6). A new level must
-- last three ticks of a slow counter. Chu: N = 19
-- (10 ms at 50 MHz); N = 3 here. The counter has no
-- reset; its starting value is for the simulator.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity db_fsm is
  port(clk, reset : in  std_logic;
       sw         : in  std_logic;
       db         : out std_logic);
end db_fsm;

architecture arch of db_fsm is
  constant N : integer := 3;
  signal q_reg : unsigned(N-1 downto 0) := (others => '0');
  signal m_tick : std_logic;
  type db_state_type is (zero, wait1_1, wait1_2, wait1_3,
                         one, wait0_1, wait0_2, wait0_3);
  signal state_reg, state_next : db_state_type;
begin
  -- the tick, once every 2^N clocks
  process(clk)
  begin
    if rising_edge(clk) then
      q_reg <= q_reg + 1;
    end if;
  end process;
  m_tick <= '1' when q_reg = 0 else '0';

  -- the FSM
  process(clk, reset)
  begin
    if reset = '1' then
      state_reg <= zero;
    elsif rising_edge(clk) then
      state_reg <= state_next;
    end if;
  end process;

  process(state_reg, sw, m_tick)
  begin
    state_next <= state_reg;
    db <= '0';
    case state_reg is
      when zero =>
        if sw = '1' then state_next <= wait1_1; end if;
      when wait1_1 =>
        if sw = '0' then state_next <= zero;
        elsif m_tick = '1' then state_next <= wait1_2; end if;
      when wait1_2 =>
        if sw = '0' then state_next <= zero;
        elsif m_tick = '1' then state_next <= wait1_3; end if;
      when wait1_3 =>
        if sw = '0' then state_next <= zero;
        elsif m_tick = '1' then state_next <= one; end if;
      when one =>
        db <= '1';
        if sw = '0' then state_next <= wait0_1; end if;
      when wait0_1 =>
        db <= '1';
        if sw = '1' then state_next <= one;
        elsif m_tick = '1' then state_next <= wait0_2; end if;
      when wait0_2 =>
        db <= '1';
        if sw = '1' then state_next <= one;
        elsif m_tick = '1' then state_next <= wait0_3; end if;
      when wait0_3 =>
        db <= '1';
        if sw = '1' then state_next <= one;
        elsif m_tick = '1' then state_next <= zero; end if;
    end case;
  end process;
end arch;
