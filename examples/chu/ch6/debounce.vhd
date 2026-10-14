-- Debouncer as an FSMD with an implicit data path
-- (Chu, Listing 6.2). Chu: N = 21 (40 ms at 50 MHz);
-- N = 3 here.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity debounce is
  port(clk, reset : in  std_logic;
       sw         : in  std_logic;
       db_level, db_tick : out std_logic);
end debounce;

architecture imp_fsmd_arch of debounce is
  constant N : integer := 3;
  type state_type is (zero, wait0, one, wait1);
  signal state_reg, state_next : state_type;
  signal q_reg, q_next : unsigned(N-1 downto 0);
begin
  -- state and data registers
  process(clk, reset)
  begin
    if reset = '1' then
      state_reg <= zero;
      q_reg <= (others => '0');
    elsif rising_edge(clk) then
      state_reg <= state_next;
      q_reg <= q_next;
    end if;
  end process;

  -- next state, and what the data path does in each
  process(state_reg, q_reg, sw, q_next)
  begin
    state_next <= state_reg;
    q_next <= q_reg;
    db_tick <= '0';
    case state_reg is
      when zero =>
        db_level <= '0';
        if sw = '1' then
          state_next <= wait1;
          q_next <= (others => '1');
        end if;
      when wait1 =>
        db_level <= '0';
        if sw = '1' then
          q_next <= q_reg - 1;
          if q_next = 0 then
            state_next <= one;
            db_tick <= '1';
          end if;
        else
          state_next <= zero;
        end if;
      when one =>
        db_level <= '1';
        if sw = '0' then
          state_next <= wait0;
          q_next <= (others => '1');
        end if;
      when wait0 =>
        db_level <= '1';
        if sw = '0' then
          q_next <= q_reg - 1;
          if q_next = 0 then
            state_next <= zero;
          end if;
        else
          state_next <= one;
        end if;
    end case;
  end process;
end imp_fsmd_arch;
