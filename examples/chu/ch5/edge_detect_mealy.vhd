-- Rising-edge detector as a Mealy machine (Chu, Listings
-- 5.4 and 5.5). As an FSM: states zero and one. With
-- zero = '0' and one = '1' it is the gate-level circuit:
-- tick = level and not (level one clock ago).
library ieee;
use ieee.std_logic_1164.all;

entity edge_detect_mealy is
  port(clk, reset : in  std_logic;
       level      : in  std_logic;
       tick       : out std_logic);
end edge_detect_mealy;

architecture mealy_arch of edge_detect_mealy is
  type state_type is (zero, one);
  signal state_reg, state_next : state_type;
begin
  process(clk, reset)
  begin
    if reset = '1' then
      state_reg <= zero;
    elsif rising_edge(clk) then
      state_reg <= state_next;
    end if;
  end process;

  process(state_reg, level)
  begin
    state_next <= state_reg;
    tick <= '0';
    case state_reg is
      when zero =>
        if level = '1' then
          state_next <= one;
          tick <= '1';               -- the Mealy output
        end if;
      when one =>
        if level = '0' then
          state_next <= zero;
        end if;
    end case;
  end process;
end mealy_arch;

-- Listing 5.5, the same circuit written as its gates:
architecture gate_level_arch of edge_detect_mealy is
  signal delay_reg : std_logic;
begin
  process(clk, reset)
  begin
    if reset = '1' then
      delay_reg <= '0';
    elsif rising_edge(clk) then
      delay_reg <= level;
    end if;
  end process;
  tick <= level and not delay_reg;
end gate_level_arch;
