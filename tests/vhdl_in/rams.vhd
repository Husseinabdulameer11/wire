-- Rams: an array signal written in a clocked process at a signal index and
-- read at the same index becomes one ram part. A 256-word ram (more than the
-- 32 words a mux of registers could pick from), a ram with starting words
-- that writes on the falling edge, and a ram of one-bit words.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rams is
  port(clk, we    : in  std_logic;
       addr       : in  std_logic_vector(7 downto 0);
       din        : in  std_logic_vector(7 downto 0);
       dout       : out std_logic_vector(7 downto 0);
       small_out  : out std_logic_vector(3 downto 0);
       bit_out    : out std_logic);
end rams;

architecture rtl of rams is
  type big_t is array (0 to 255) of std_logic_vector(7 downto 0);
  signal big : big_t := (others => (others => '0'));
  type small_t is array (0 to 7) of std_logic_vector(3 downto 0);
  signal small : small_t := (0 => "0101", 3 => "1010", others => (others => '0'));
  type bits_t is array (0 to 15) of std_logic_vector(0 downto 0);
  signal bits : bits_t := (others => "0");
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if we = '1' then
        big(to_integer(unsigned(addr))) <= din;
      end if;
    end if;
  end process;
  dout <= big(to_integer(unsigned(addr)));

  process (clk)
  begin
    if falling_edge(clk) then
      if we = '1' and din(7) = '1' then
        small(to_integer(unsigned(addr(2 downto 0)))) <= din(3 downto 0);
      end if;
    end if;
  end process;
  small_out <= small(to_integer(unsigned(addr(2 downto 0))));

  process (clk)
  begin
    if rising_edge(clk) then
      if we = '1' then
        bits(to_integer(unsigned(addr(3 downto 0))))(0) <= din(0);
      end if;
    end if;
  end process;
  bit_out <= bits(to_integer(unsigned(addr(3 downto 0))))(0);
end rtl;
