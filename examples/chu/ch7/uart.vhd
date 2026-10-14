-- Baud-rate tick: a mod-M counter (Chu, Listing 4.11).
-- Chu: M = 163 (19200 baud x 16 at 50 MHz); M = 4 here.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mod_m_counter is
  generic(N : integer := 2;
          M : integer := 4);
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


-- FIFO buffer, a circular queue in a register file
-- (Chu, Listing 4.20). W = 2 here (4 words), 4 in Chu.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity fifo is
  generic(B : natural := 8;    -- bits per word
          W : natural := 2);   -- address bits
  port(clk, reset  : in  std_logic;
       rd, wr      : in  std_logic;
       w_data      : in  std_logic_vector(B-1 downto 0);
       empty, full : out std_logic;
       r_data      : out std_logic_vector(B-1 downto 0));
end fifo;

architecture arch of fifo is
  type reg_file_type is array (0 to 2**W-1) of
       std_logic_vector(B-1 downto 0);
  signal array_reg : reg_file_type;
  signal w_ptr_reg, w_ptr_next, w_ptr_succ : unsigned(W-1 downto 0);
  signal r_ptr_reg, r_ptr_next, r_ptr_succ : unsigned(W-1 downto 0);
  signal full_reg, empty_reg, full_next, empty_next : std_logic;
  signal wr_en : std_logic;
  signal op    : std_logic_vector(1 downto 0);
begin
  -- the register file; writing needs a free place
  process(clk, reset)
  begin
    if reset = '1' then
      array_reg <= (others => (others => '0'));
    elsif rising_edge(clk) then
      if wr_en = '1' then
        array_reg(to_integer(w_ptr_reg)) <= w_data;
      end if;
    end if;
  end process;
  r_data <= array_reg(to_integer(r_ptr_reg));
  wr_en  <= wr and not full_reg;

  -- the control: two pointers and two flags
  process(clk, reset)
  begin
    if reset = '1' then
      w_ptr_reg <= (others => '0');
      r_ptr_reg <= (others => '0');
      full_reg  <= '0';
      empty_reg <= '1';
    elsif rising_edge(clk) then
      w_ptr_reg <= w_ptr_next;
      r_ptr_reg <= r_ptr_next;
      full_reg  <= full_next;
      empty_reg <= empty_next;
    end if;
  end process;
  w_ptr_succ <= w_ptr_reg + 1;
  r_ptr_succ <= r_ptr_reg + 1;
  op <= wr & rd;
  process(all)
  begin
    w_ptr_next <= w_ptr_reg;
    r_ptr_next <= r_ptr_reg;
    full_next  <= full_reg;
    empty_next <= empty_reg;
    case op is
      when "01" =>                       -- read
        if empty_reg = '0' then
          r_ptr_next <= r_ptr_succ;
          full_next  <= '0';
          if r_ptr_succ = w_ptr_reg then
            empty_next <= '1';
          end if;
        end if;
      when "10" =>                       -- write
        if full_reg = '0' then
          w_ptr_next <= w_ptr_succ;
          empty_next <= '0';
          if w_ptr_succ = r_ptr_reg then
            full_next <= '1';
          end if;
        end if;
      when "11" =>                       -- both
        w_ptr_next <= w_ptr_succ;
        r_ptr_next <= r_ptr_succ;
      when others =>                     -- nothing
        null;
    end case;
  end process;
  full  <= full_reg;
  empty <= empty_reg;
end arch;

-- UART receiver (Chu, Listing 7.1): 16 ticks per bit,
-- sample in the middle of each bit, LSB first.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity uart_rx is
  generic(DBIT    : integer := 8;     -- data bits
          SB_TICK : integer := 16);   -- ticks for the stop bit
  port(clk, reset   : in  std_logic;
       rx           : in  std_logic;
       s_tick       : in  std_logic;
       rx_done_tick : out std_logic;
       dout         : out std_logic_vector(7 downto 0));
end uart_rx;

architecture arch of uart_rx is
  type state_type is (idle, start, data, stop);
  signal state_reg, state_next : state_type;
  signal s_reg, s_next : unsigned(3 downto 0);   -- ticks
  signal n_reg, n_next : unsigned(2 downto 0);   -- bits
  signal b_reg, b_next : std_logic_vector(7 downto 0);
begin
  process(clk, reset)
  begin
    if reset = '1' then
      state_reg <= idle;
      s_reg <= (others => '0');
      n_reg <= (others => '0');
      b_reg <= (others => '0');
    elsif rising_edge(clk) then
      state_reg <= state_next;
      s_reg <= s_next;
      n_reg <= n_next;
      b_reg <= b_next;
    end if;
  end process;

  process(state_reg, s_reg, n_reg, b_reg, s_tick, rx)
  begin
    state_next <= state_reg;
    s_next <= s_reg;
    n_next <= n_reg;
    b_next <= b_reg;
    rx_done_tick <= '0';
    case state_reg is
      when idle =>
        if rx = '0' then                 -- a start bit begins
          state_next <= start;
          s_next <= (others => '0');
        end if;
      when start =>                      -- to its middle
        if s_tick = '1' then
          if s_reg = 7 then
            state_next <= data;
            s_next <= (others => '0');
            n_next <= (others => '0');
          else
            s_next <= s_reg + 1;
          end if;
        end if;
      when data =>                       -- one bit every 16
        if s_tick = '1' then
          if s_reg = 15 then
            s_next <= (others => '0');
            b_next <= rx & b_reg(7 downto 1);
            if n_reg = DBIT - 1 then
              state_next <= stop;
            else
              n_next <= n_reg + 1;
            end if;
          else
            s_next <= s_reg + 1;
          end if;
        end if;
      when stop =>
        if s_tick = '1' then
          if s_reg = SB_TICK - 1 then
            state_next <= idle;
            rx_done_tick <= '1';
          else
            s_next <= s_reg + 1;
          end if;
        end if;
    end case;
  end process;
  dout <= b_reg;
end arch;

-- UART transmitter (Chu, Listing 7.3): start bit, 8 data
-- bits LSB first, stop bit; tx comes from a register.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity uart_tx is
  generic(DBIT    : integer := 8;
          SB_TICK : integer := 16);
  port(clk, reset   : in  std_logic;
       tx_start     : in  std_logic;
       s_tick       : in  std_logic;
       din          : in  std_logic_vector(7 downto 0);
       tx_done_tick : out std_logic;
       tx           : out std_logic);
end uart_tx;

architecture arch of uart_tx is
  type state_type is (idle, start, data, stop);
  signal state_reg, state_next : state_type;
  signal s_reg, s_next : unsigned(3 downto 0);
  signal n_reg, n_next : unsigned(2 downto 0);
  signal b_reg, b_next : std_logic_vector(7 downto 0);
  signal tx_reg, tx_next : std_logic;
begin
  process(clk, reset)
  begin
    if reset = '1' then
      state_reg <= idle;
      s_reg <= (others => '0');
      n_reg <= (others => '0');
      b_reg <= (others => '0');
      tx_reg <= '1';                     -- the line idles high
    elsif rising_edge(clk) then
      state_reg <= state_next;
      s_reg <= s_next;
      n_reg <= n_next;
      b_reg <= b_next;
      tx_reg <= tx_next;
    end if;
  end process;

  process(state_reg, s_reg, n_reg, b_reg, s_tick, tx_reg, tx_start, din)
  begin
    state_next <= state_reg;
    s_next <= s_reg;
    n_next <= n_reg;
    b_next <= b_reg;
    tx_next <= tx_reg;
    tx_done_tick <= '0';
    case state_reg is
      when idle =>
        tx_next <= '1';
        if tx_start = '1' then
          state_next <= start;
          s_next <= (others => '0');
          b_next <= din;
        end if;
      when start =>
        tx_next <= '0';                  -- the start bit
        if s_tick = '1' then
          if s_reg = 15 then
            state_next <= data;
            s_next <= (others => '0');
            n_next <= (others => '0');
          else
            s_next <= s_reg + 1;
          end if;
        end if;
      when data =>
        tx_next <= b_reg(0);             -- LSB first
        if s_tick = '1' then
          if s_reg = 15 then
            s_next <= (others => '0');
            b_next <= '0' & b_reg(7 downto 1);
            if n_reg = DBIT - 1 then
              state_next <= stop;
            else
              n_next <= n_reg + 1;
            end if;
          else
            s_next <= s_reg + 1;
          end if;
        end if;
      when stop =>
        tx_next <= '1';                  -- the stop bit
        if s_tick = '1' then
          if s_reg = SB_TICK - 1 then
            state_next <= idle;
            tx_done_tick <= '1';
          else
            s_next <= s_reg + 1;
          end if;
        end if;
    end case;
  end process;
  tx <= tx_reg;
end arch;

-- The complete UART (Chu, Listing 7.4): a baud-rate
-- tick, a receiver and a transmitter, each with a FIFO.
library ieee;
use ieee.std_logic_1164.all;

entity uart is
  generic(DBIT     : integer := 8;
          SB_TICK  : integer := 16;
          DVSR     : integer := 4;     -- 163 on the board
          DVSR_BIT : integer := 2;
          FIFO_W   : integer := 2);    -- 2^2 words a FIFO
  port(clk, reset : in  std_logic;
       rd_uart, wr_uart : in  std_logic;
       rx       : in  std_logic;
       w_data   : in  std_logic_vector(7 downto 0);
       tx_full, rx_empty : out std_logic;
       r_data   : out std_logic_vector(7 downto 0);
       tx       : out std_logic);
end uart;

architecture str_arch of uart is
  signal tick, rx_done_tick, tx_done_tick : std_logic;
  signal tx_empty, tx_fifo_not_empty : std_logic;
  signal tx_fifo_out, rx_data_out : std_logic_vector(7 downto 0);
begin
  baud_gen_unit: entity work.mod_m_counter(arch)
    generic map(M => DVSR, N => DVSR_BIT)
    port map(clk => clk, reset => reset, q => open, max_tick => tick);
  uart_rx_unit: entity work.uart_rx(arch)
    generic map(DBIT => DBIT, SB_TICK => SB_TICK)
    port map(clk => clk, reset => reset, rx => rx, s_tick => tick,
             rx_done_tick => rx_done_tick, dout => rx_data_out);
  fifo_rx_unit: entity work.fifo(arch)
    generic map(B => DBIT, W => FIFO_W)
    port map(clk => clk, reset => reset, rd => rd_uart,
             wr => rx_done_tick, w_data => rx_data_out,
             empty => rx_empty, full => open, r_data => r_data);
  fifo_tx_unit: entity work.fifo(arch)
    generic map(B => DBIT, W => FIFO_W)
    port map(clk => clk, reset => reset, rd => tx_done_tick,
             wr => wr_uart, w_data => w_data, empty => tx_empty,
             full => tx_full, r_data => tx_fifo_out);
  uart_tx_unit: entity work.uart_tx(arch)
    generic map(DBIT => DBIT, SB_TICK => SB_TICK)
    port map(clk => clk, reset => reset, tx_start => tx_fifo_not_empty,
             s_tick => tick, din => tx_fifo_out,
             tx_done_tick => tx_done_tick, tx => tx);
  tx_fifo_not_empty <= not tx_empty;
end str_arch;
