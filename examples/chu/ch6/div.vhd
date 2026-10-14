-- Long division of W-bit unsigned numbers, an FSMD
-- (Chu, Listing 6.5).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity div is
  generic(W    : integer := 8;
          CBIT : integer := 4);     -- log2(W) + 1
  port(clk, reset : in  std_logic;
       start      : in  std_logic;
       dvsr, dvnd : in  std_logic_vector(W-1 downto 0);
       ready, done_tick : out std_logic;
       quo, rmd   : out std_logic_vector(W-1 downto 0));
end div;

architecture arch of div is
  type state_type is (idle, op, last, done);
  signal state_reg, state_next : state_type;
  signal rh_reg, rh_next, rh_tmp, d_reg, d_next : unsigned(W-1 downto 0);
  signal rl_reg, rl_next : std_logic_vector(W-1 downto 0);
  signal n_reg, n_next : unsigned(CBIT-1 downto 0);
  signal q_bit : std_logic;
begin
  process(clk, reset)
  begin
    if reset = '1' then
      state_reg <= idle;
      rh_reg <= (others => '0');
      rl_reg <= (others => '0');
      d_reg  <= (others => '0');
      n_reg  <= (others => '0');
    elsif rising_edge(clk) then
      state_reg <= state_next;
      rh_reg <= rh_next;
      rl_reg <= rl_next;
      d_reg  <= d_next;
      n_reg  <= n_next;
    end if;
  end process;

  -- compare and subtract
  rh_tmp <= rh_reg - d_reg when rh_reg >= d_reg else rh_reg;
  q_bit  <= '1' when rh_reg >= d_reg else '0';

  process(all)
  begin
    ready <= '0';
    done_tick <= '0';
    state_next <= state_reg;
    rh_next <= rh_reg;
    rl_next <= rl_reg;
    d_next  <= d_reg;
    n_next  <= n_reg;
    case state_reg is
      when idle =>
        ready <= '1';
        if start = '1' then
          rh_next <= (others => '0');
          rl_next <= dvnd;
          d_next  <= unsigned(dvsr);
          n_next  <= to_unsigned(W + 1, CBIT);
          state_next <= op;
        end if;
      when op =>                       -- shift rh and rl left
        rl_next <= rl_reg(W-2 downto 0) & q_bit;
        rh_next <= rh_tmp(W-2 downto 0) & rl_reg(W-1);
        n_next  <= n_reg - 1;
        if n_reg - 1 = 1 then
          state_next <= last;
        end if;
      when last =>                     -- the last step
        rl_next <= rl_reg(W-2 downto 0) & q_bit;
        rh_next <= rh_tmp;
        state_next <= done;
      when done =>
        done_tick <= '1';
        state_next <= idle;
    end case;
  end process;
  quo <= rl_reg;
  rmd <= std_logic_vector(rh_reg);
end arch;
