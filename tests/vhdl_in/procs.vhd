-- Processes: a clocked process with an asynchronous reset and an enable, a
-- state machine with an enumeration type, a register file (an array signal
-- written and read with a signal index), a bit picked by a signal on both
-- sides of <=, a combinational process with a case and a for loop with exit,
-- integer variables with a range, and functions (an integer argument, an
-- unconstrained vector, a local variable, return in an if, a range choice in
-- an aggregate).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity procs is
  port(clk, reset : in  std_logic;
       go, we     : in  std_logic;
       addr       : in  std_logic_vector(1 downto 0);
       din        : in  std_logic_vector(7 downto 0);
       sel        : in  std_logic_vector(2 downto 0);
       dout       : out std_logic_vector(7 downto 0);
       onehot     : out std_logic_vector(7 downto 0);
       picked     : out std_logic;
       first1     : out std_logic_vector(2 downto 0);
       busy, done : out std_logic;
       count      : out std_logic_vector(3 downto 0);
       masked     : out std_logic_vector(7 downto 0);
       big        : out std_logic);
end procs;

architecture rtl of procs is
  type state_t is (idle, run, finish);
  signal state, state_next : state_t;
  type regs_t is array (0 to 3) of std_logic_vector(7 downto 0);
  signal regs : regs_t := (others => (others => '0'));
  signal cnt : unsigned(3 downto 0);

  function fill(e : std_logic) return std_logic_vector is
  begin
    return (7 downto 0 => e);
  end function;

  function clamp(x : unsigned; lim : integer) return unsigned is
    variable r : unsigned(x'length - 1 downto 0);
  begin
    r := x;
    if x > lim then
      return to_unsigned(lim, x'length);
    end if;
    return r;
  end clamp;
begin
  -- the state register, with an asynchronous reset
  process(clk, reset)
  begin
    if reset = '1' then
      state <= idle;
      cnt <= (others => '0');
    elsif rising_edge(clk) then
      state <= state_next;
      if state = run then
        cnt <= cnt + 1;
      elsif state = idle then
        cnt <= (others => '0');
      end if;
    end if;
  end process;

  -- the next state
  process(state, go, cnt)
  begin
    state_next <= state;
    case state is
      when idle   => if go = '1' then state_next <= run; end if;
      when run    => if cnt = 9 then state_next <= finish; end if;
      when finish => state_next <= idle;
    end case;
  end process;
  busy  <= '1' when state = run else '0';
  done  <= '1' when state = finish else '0';
  count <= std_logic_vector(clamp(cnt, 7));
  big   <= '1' when clamp(cnt, 7) = 7 else '0';

  -- a register file: written with a signal index, read with one
  process(clk)
  begin
    if rising_edge(clk) then
      if we = '1' then
        regs(to_integer(unsigned(addr))) <= din;
      end if;
    end if;
  end process;
  dout   <= regs(to_integer(unsigned(addr)));
  masked <= din and fill(go);

  -- bits picked by a signal
  process(sel)
  begin
    onehot <= (others => '0');
    onehot(to_integer(unsigned(sel))) <= '1';
  end process;
  picked <= din(to_integer(unsigned(sel)));

  -- the lowest 1 in din, with a for loop and exit
  process(din)
    variable i : integer range 0 to 7;
  begin
    i := 7;
    for k in 0 to 7 loop
      if din(k) = '1' then
        i := k;
        exit;
      end if;
    end loop;
    first1 <= std_logic_vector(to_unsigned(i, 3));
  end process;
end rtl;
