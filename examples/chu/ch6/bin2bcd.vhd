-- Binary to BCD, shift and add 3 (Chu, Listing 6.6).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bin2bcd is
  port(clk, reset : in  std_logic;
       start      : in  std_logic;
       bin        : in  std_logic_vector(12 downto 0);
       ready, done_tick : out std_logic;
       bcd3, bcd2, bcd1, bcd0 : out std_logic_vector(3 downto 0));
end bin2bcd;

architecture arch of bin2bcd is
  type state_type is (idle, op, done);
  type digits is array (0 to 3) of unsigned(3 downto 0);
  signal state_reg, state_next : state_type;
  signal p2s_reg, p2s_next : std_logic_vector(12 downto 0);
  signal n_reg, n_next : unsigned(3 downto 0);
  signal bcd_reg, bcd_next, bcd_tmp : digits;
begin
  process(clk, reset)
  begin
    if reset = '1' then
      state_reg <= idle;
      p2s_reg <= (others => '0');
      n_reg   <= (others => '0');
      bcd_reg <= (others => (others => '0'));
    elsif rising_edge(clk) then
      state_reg <= state_next;
      p2s_reg <= p2s_next;
      n_reg   <= n_next;
      bcd_reg <= bcd_next;
    end if;
  end process;

  -- the four adjust circuits: + 3 when above 4
  adjust: for k in 0 to 3 generate
    bcd_tmp(k) <= bcd_reg(k) + 3 when bcd_reg(k) > 4 else bcd_reg(k);
  end generate;

  process(all)
  begin
    state_next <= state_reg;
    ready <= '0';
    done_tick <= '0';
    p2s_next <= p2s_reg;
    n_next   <= n_reg;
    bcd_next <= bcd_reg;
    case state_reg is
      when idle =>
        ready <= '1';
        if start = '1' then
          bcd_next <= (others => (others => '0'));
          n_next   <= to_unsigned(13, 4);
          p2s_next <= bin;
          state_next <= op;
        end if;
      when op =>
        -- shift the binary bit into the digits, adjusted
        p2s_next <= p2s_reg(11 downto 0) & '0';
        bcd_next(0) <= bcd_tmp(0)(2 downto 0) & p2s_reg(12);
        for k in 1 to 3 loop
          bcd_next(k) <= bcd_tmp(k)(2 downto 0) & bcd_tmp(k-1)(3);
        end loop;
        n_next <= n_reg - 1;
        if n_reg - 1 = 0 then
          state_next <= done;
        end if;
      when done =>
        done_tick <= '1';
        state_next <= idle;
    end case;
  end process;
  bcd3 <= std_logic_vector(bcd_reg(3));
  bcd2 <= std_logic_vector(bcd_reg(2));
  bcd1 <= std_logic_vector(bcd_reg(1));
  bcd0 <= std_logic_vector(bcd_reg(0));
end arch;
