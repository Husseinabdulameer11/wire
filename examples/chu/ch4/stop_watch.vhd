-- Stopwatch, three BCD digits (Chu, Listings 4.17, 4.18).
-- Chu's DVSR is 5_000_000 (0.1 s at 50 MHz); 4 here.
-- The tick also needs go, so a pause never runs the
-- digits (Chu's ms_tick looks only at the count).
-- No reset input: the starting values are for the
-- simulator; clr clears it at a clock edge.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity stop_watch is
  port(clk        : in  std_logic;
       go, clr    : in  std_logic;
       d2, d1, d0 : out std_logic_vector(3 downto 0));
end stop_watch;

architecture if_arch of stop_watch is
  constant DVSR : integer := 4;
  signal ms_reg, ms_next : unsigned(2 downto 0) := (others => '0');
  signal d2_reg, d1_reg, d0_reg : unsigned(3 downto 0) := (others => '0');
  signal d2_next, d1_next, d0_next : unsigned(3 downto 0);
  signal ms_tick : std_logic;
begin
  process(clk)
  begin
    if rising_edge(clk) then
      ms_reg <= ms_next;
      d2_reg <= d2_next;
      d1_reg <= d1_next;
      d0_reg <= d0_next;
    end if;
  end process;

  -- the 0.1 s tick: a mod-(DVSR+1) counter, running while go
  ms_next <= (others => '0') when clr = '1' or (ms_reg = DVSR and go = '1') else
             ms_reg + 1      when go = '1' else
             ms_reg;
  ms_tick <= '1' when ms_reg = DVSR and go = '1' else '0';

  -- three digits, each carrying into the next
  process(d0_reg, d1_reg, d2_reg, ms_tick, clr)
  begin
    d0_next <= d0_reg;
    d1_next <= d1_reg;
    d2_next <= d2_reg;
    if clr = '1' then
      d0_next <= "0000";
      d1_next <= "0000";
      d2_next <= "0000";
    elsif ms_tick = '1' then
      if d0_reg /= 9 then
        d0_next <= d0_reg + 1;
      else                                -- xx9
        d0_next <= "0000";
        if d1_reg /= 9 then
          d1_next <= d1_reg + 1;
        else                              -- x99
          d1_next <= "0000";
          if d2_reg /= 9 then
            d2_next <= d2_reg + 1;
          else                            -- 999
            d2_next <= "0000";
          end if;
        end if;
      end if;
    end if;
  end process;
  d0 <= std_logic_vector(d0_reg);
  d1 <= std_logic_vector(d1_reg);
  d2 <= std_logic_vector(d2_reg);
end if_arch;
