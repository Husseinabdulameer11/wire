-- FSM with a Moore and a Mealy output (Chu, Listing 5.1):
-- state register, next-state logic, output logic, each
-- in its own segment.
library ieee;
use ieee.std_logic_1164.all;

entity fsm_eg is
  port(clk, reset : in  std_logic;
       a, b       : in  std_logic;
       y0, y1     : out std_logic);
end fsm_eg;

architecture mult_seg_arch of fsm_eg is
  type eg_state_type is (s0, s1, s2);
  signal state_reg, state_next : eg_state_type;
begin
  -- state register
  process(clk, reset)
  begin
    if reset = '1' then
      state_reg <= s0;
    elsif rising_edge(clk) then
      state_reg <= state_next;
    end if;
  end process;

  -- next-state logic
  process(state_reg, a, b)
  begin
    case state_reg is
      when s0 =>
        if a = '1' and b = '1' then
          state_next <= s2;
        elsif a = '1' then
          state_next <= s1;
        else
          state_next <= s0;
        end if;
      when s1 =>
        if a = '1' then
          state_next <= s0;
        else
          state_next <= s1;
        end if;
      when s2 =>
        state_next <= s0;
    end case;
  end process;

  -- Moore output: the state alone
  y1 <= '1' when state_reg = s1 else '0';
  -- Mealy output: the state and the inputs
  y0 <= '1' when state_reg = s0 and a = '1' and b = '1' else '0';
end mult_seg_arch;
