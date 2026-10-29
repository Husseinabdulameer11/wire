-- Multiplication and values wider than 32 bits: products as wide as both
-- sides (RISC-V's MUL, MULH, MULHU and MULHSU), 64-bit signals and
-- variables kept as 32-bit wires, a 64-bit multiply-accumulate register,
-- and wide slices, &, resize, shifts, logic, comparisons, when/else,
-- with/select and negation.
-- a chip with a 48-bit register inside, and an output that goes to part of a
-- wide signal outside
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mac48 is
  port(clk, clr : in  std_logic;
       x, y     : in  std_logic_vector(15 downto 0);
       hi, lo   : out std_logic_vector(23 downto 0));
end mac48;

architecture rtl of mac48 is
  signal acc : unsigned(47 downto 0) := (others => '0');
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if clr = '1' then acc <= (others => '0');
      else acc <= acc + unsigned(x) * unsigned(y);
      end if;
    end if;
  end process;
  hi <= std_logic_vector(acc(47 downto 24));
  lo <= std_logic_vector(acc(23 downto 0));
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mul_wide is
  port(clk, rst : in  std_logic;
       a, b     : in  std_logic_vector(31 downto 0);
       op       : in  std_logic_vector(1 downto 0);
       x8       : in  std_logic_vector(7 downto 0);
       y16      : in  std_logic_vector(15 downto 0);
       mul_lo, mulh, mulhu, mulhsu : out std_logic_vector(31 downto 0);
       alu      : out std_logic_vector(31 downto 0);
       small    : out std_logic_vector(23 downto 0);
       times10  : out std_logic_vector(15 downto 0);
       acc_lo, acc_hi : out std_logic_vector(31 downto 0);
       big, same, neg_hi, shifted, mixed, picked : out std_logic_vector(31 downto 0);
       p96_hi, p96_mid, s80_hi, s80_lo : out std_logic_vector(31 downto 0);
       mac_top : out std_logic_vector(23 downto 0);
       top_bit, is_big : out std_logic);
end mul_wide;

architecture rtl of mul_wide is
  signal uu, ss : unsigned(63 downto 0);
  signal ss_s   : signed(63 downto 0);
  signal su     : signed(64 downto 0);
  signal acc    : unsigned(63 downto 0) := (others => '0');
  signal w      : std_logic_vector(63 downto 0);
  signal sel64  : std_logic_vector(63 downto 0);
  signal negd, sra20 : signed(63 downto 0);
  signal p96 : unsigned(95 downto 0);
  signal s80 : signed(79 downto 0);
  signal m48 : std_logic_vector(47 downto 0);
begin
  uu   <= unsigned(a) * unsigned(b);
  ss_s <= signed(a) * signed(b);
  ss   <= unsigned(ss_s);
  su   <= signed(a) * signed('0' & b);
  mul_lo <= std_logic_vector(uu(31 downto 0));
  mulh   <= std_logic_vector(ss(63 downto 32));
  mulhu  <= std_logic_vector(uu(63 downto 32));
  mulhsu <= std_logic_vector(su(63 downto 32));

  -- the way a RISC-V ALU writes it: a 64-bit variable in a process
  process (a, b, op)
    variable p : signed(65 downto 0);
  begin
    case op is
      when "00" => p := signed('0' & a) * signed('0' & b);
      when "01" => p := signed(a(31) & a) * signed(b(31) & b);
      when "10" => p := signed(a(31) & a) * signed('0' & b);
      when others => p := (others => '0');
    end case;
    if op = "11" then
      alu <= std_logic_vector(resize(unsigned(a) * unsigned(b), 32));
    else
      alu <= std_logic_vector(p(63 downto 32));
    end if;
  end process;

  small   <= std_logic_vector(unsigned(y16) * unsigned(x8));
  times10 <= std_logic_vector(unsigned(x8) * 10);

  -- multiply-accumulate: a 64-bit register
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        acc <= (others => '0');
      else
        acc <= acc + resize(unsigned(x8) * unsigned(y16), 64);
      end if;
    end if;
  end process;
  acc_lo <= std_logic_vector(acc(31 downto 0));
  acc_hi <= std_logic_vector(acc(63 downto 32));

  w <= a & b;
  big     <= w(47 downto 16);
  same    <= std_logic_vector(uu(63 downto 32) xor (not uu(31 downto 0)));
  negd    <= -ss_s;
  sra20   <= shift_right(ss_s, 20);
  neg_hi  <= std_logic_vector(negd(63 downto 32));
  shifted <= std_logic_vector(sra20(31 downto 0));
  mixed   <= w(63 downto 32) when uu > x"00000000FFFFFFFF" else w(31 downto 0);
  top_bit <= ss_s(63);
  is_big  <= '1' when ss_s < -1000 and uu /= 0 else '0';

  with op select sel64 <=
    std_logic_vector(uu)        when "00",
    std_logic_vector(ss_s)      when "01",
    not w                       when "10",
    (others => '1')             when others;
  picked <= sel64(55 downto 24);

  -- wider than 32 bits on each side: multiplied 32 bits at a time
  p96 <= unsigned(a(15 downto 0) & b) * unsigned(b & a(15 downto 0));
  s80 <= signed(x8 & b) * signed(b & x8);
  p96_hi  <= std_logic_vector(p96(95 downto 64));
  p96_mid <= std_logic_vector(p96(63 downto 32));
  s80_hi  <= std_logic_vector(s80(79 downto 48));
  s80_lo  <= std_logic_vector(s80(31 downto 0));

  mac: entity work.mac48 port map(clk => clk, clr => rst, x => y16, y => b(15 downto 0),
                                  hi => m48(47 downto 24), lo => m48(23 downto 0));
  mac_top <= m48(47 downto 24) xor m48(23 downto 0);
end rtl;
