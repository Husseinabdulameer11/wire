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
