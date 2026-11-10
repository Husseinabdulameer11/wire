-- arrays with two indexes (m(i, j)) and arrays of arrays (g(i)(j)), read and
-- written with signals; an array variable; and an array of 64 elements read
-- with a signal (more than one mux can take)
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity arrays2 is
  port (clk, we : in std_logic;
        r, c : in std_logic_vector(1 downto 0);
        i6 : in std_logic_vector(5 downto 0);
        d : in std_logic_vector(3 downto 0);
        m_out, g_out, sum_out : out std_logic_vector(3 downto 0);
        big_out : out std_logic_vector(7 downto 0);
        diag : out std_logic_vector(15 downto 0));
end arrays2;
architecture rtl of arrays2 is
  type grid_t is array (0 to 3, 0 to 3) of std_logic_vector(3 downto 0);
  type row_t is array (0 to 3) of std_logic_vector(3 downto 0);
  type rows_t is array (0 to 3) of row_t;
  type big_t is array (0 to 63) of std_logic_vector(7 downto 0);
  signal m : grid_t := (others => (others => "0000"));
  signal g : rows_t := (others => (others => "0000"));
  function fill return big_t is
    variable t : big_t;
  begin
    for k in 0 to 63 loop
      t(k) := std_logic_vector(to_unsigned(k * 3 + 1, 8));
    end loop;
    return t;
  end function;
  constant BIG : big_t := fill;
  signal bigs : big_t := fill;
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if we = '1' then
        m(to_integer(unsigned(r)), to_integer(unsigned(c))) <= d;
        g(to_integer(unsigned(c)))(to_integer(unsigned(r))) <= not d;
        bigs(to_integer(unsigned(i6))) <= d & d;
      end if;
    end if;
  end process;
  m_out <= m(to_integer(unsigned(r)), to_integer(unsigned(c)));
  g_out <= g(to_integer(unsigned(r)))(to_integer(unsigned(c)));
  big_out <= bigs(to_integer(unsigned(i6))) xor BIG(to_integer(unsigned(i6)));
  diag <= m(0, 0) & m(1, 1) & m(2, 2) & g(3)(3);
  process (all)
    variable acc : row_t;
    variable s : unsigned(3 downto 0);
  begin
    for k in 0 to 3 loop
      acc(k) := m(k, to_integer(unsigned(c)));
    end loop;
    s := (others => '0');
    for k in 0 to 3 loop
      s := s + unsigned(acc(k));
    end loop;
    sum_out <= std_logic_vector(s);
  end process;
end rtl;
