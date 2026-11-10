-- functions and procedures: overloads by type, defaults and named arguments,
-- recursion, return inside a loop, arrays in and out, procedures with out
-- and inout parameters (variables and signals), called in and outside a process
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity subprograms is
  generic (N : integer := 8);
  port (clk : in std_logic;
        a, b : in std_logic_vector(7 downto 0);
        sel : in std_logic_vector(1 downto 0);
        first1 : out std_logic_vector(3 downto 0);
        par, par2 : out std_logic;
        sum : out std_logic_vector(7 downto 0);
        mx, mn : out std_logic_vector(7 downto 0);
        rev : out std_logic_vector(7 downto 0);
        tot : out std_logic_vector(9 downto 0);
        acc : out std_logic_vector(7 downto 0);
        bits : out std_logic_vector(3 downto 0));
end subprograms;
architecture rtl of subprograms is
  type quad is array (0 to 3) of unsigned(7 downto 0);
  signal q : quad;
  signal racc : unsigned(7 downto 0) := (others => '0');

  -- the same name for a bit and a vector
  function parity(x : std_logic) return std_logic is
  begin
    return x;
  end function;
  function parity(x : std_logic_vector) return std_logic is
    variable p : std_logic := '0';
  begin
    for i in x'range loop
      p := p xor parity(x(i));
    end loop;
    return p;
  end function;

  -- recursion, on a constant
  function clog2(n : integer) return integer is
  begin
    if n <= 1 then return 0; end if;
    return 1 + clog2((n + 1) / 2);
  end function;

  -- return inside a loop: the first 1 from the bottom
  function first_one(x : std_logic_vector(7 downto 0)) return unsigned is
  begin
    for i in 0 to 7 loop
      if x(i) = '1' then
        return to_unsigned(i, 4);
      end if;
    end loop;
    return to_unsigned(15, 4);
  end function;

  -- a default and named arguments
  function addk(x : unsigned; k : integer := 1; wrap : boolean := true) return unsigned is
  begin
    if wrap then return x + k; end if;
    return x;
  end function;

  -- arrays in and out
  function total(v : quad) return unsigned is
    variable s : unsigned(9 downto 0) := (others => '0');
  begin
    for i in v'range loop
      s := s + resize(v(i), 10);
    end loop;
    return s;
  end function;
  function swapped(v : quad) return quad is
    variable r : quad;
  begin
    for i in 0 to 3 loop
      r(i) := v(3 - i);
    end loop;
    return r;
  end function;

  procedure minmax(x, y : in unsigned(7 downto 0); variable hi, lo : out unsigned(7 downto 0)) is
  begin
    if x > y then hi := x; lo := y; else hi := y; lo := x; end if;
  end procedure;

  procedure reverse(signal x : in std_logic_vector(7 downto 0); signal y : out std_logic_vector(7 downto 0)) is
  begin
    for i in 0 to 7 loop
      y(i) <= x(7 - i);
    end loop;
  end procedure;

  procedure bump(variable v : inout unsigned(7 downto 0); by : in unsigned(7 downto 0)) is
  begin
    if by = 0 then
      return;   -- a return ends only the procedure
    end if;
    v := v + by;
  end procedure;
begin
  first1 <= std_logic_vector(first_one(a));
  par <= parity(a);
  par2 <= parity(a(0)) xor parity(b);
  sum <= std_logic_vector(addk(unsigned(a), wrap => true, k => 3)) when sel = "00" else
         std_logic_vector(addk(unsigned(b))) when sel = "01" else
         std_logic_vector(addk(unsigned(a), 5, false));
  bits <= std_logic_vector(to_unsigned(clog2(N) + clog2(5), 4));

  process (a, b)
    variable h, l : unsigned(7 downto 0);
  begin
    minmax(unsigned(a), unsigned(b), h, l);
    mx <= std_logic_vector(h);
    mn <= std_logic_vector(l);
  end process;

  reverse(a, rev);

  process (a, b)
    variable t : quad;
  begin
    t := (unsigned(a), unsigned(b), unsigned(a xor b), unsigned(a and b));
    q <= swapped(t);
  end process;
  tot <= std_logic_vector(total(q));

  process (clk)
    variable v : unsigned(7 downto 0);
  begin
    if rising_edge(clk) then
      v := racc;
      bump(v, unsigned(b));
      bump(v, x"00");
      racc <= v;
    end if;
  end process;
  acc <= std_logic_vector(racc);
end rtl;
