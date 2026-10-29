-- Byte lanes: a ram of 32-bit words written a byte at a time
-- (mem(i)(7 downto 0) <= ..., under if we then if be(k) then), and
-- two rams written in the same process. Each write becomes a ram part
-- of its own, and a read of the word puts the lanes side by side.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity byte_lanes is
  port(clk, we  : in  std_logic;
       be       : in  std_logic_vector(3 downto 0);
       addr     : in  std_logic_vector(5 downto 0);
       din      : in  std_logic_vector(31 downto 0);
       dout     : out std_logic_vector(31 downto 0);
       pair     : out std_logic_vector(15 downto 0);
       top_bit  : out std_logic);
end byte_lanes;

architecture rtl of byte_lanes is
  type word_t is array (0 to 63) of std_logic_vector(31 downto 0);
  signal mem : word_t := (0 => x"12345678", 5 => x"CAFEF00D", others => (others => '0'));
  type byte_t is array (0 to 63) of std_logic_vector(7 downto 0);
  signal lo_b, hi_b : byte_t := (others => (others => '0'));
  signal i : integer range 0 to 63;
begin
  i <= to_integer(unsigned(addr));

  process (clk)
  begin
    if rising_edge(clk) then
      if we = '1' then
        if be(0) = '1' then mem(i)(7 downto 0)   <= din(7 downto 0);   end if;
        if be(1) = '1' then mem(i)(15 downto 8)  <= din(15 downto 8);  end if;
        if be(2) = '1' then mem(i)(23 downto 16) <= din(23 downto 16); end if;
        if be(3) = '1' then mem(i)(31 downto 24) <= din(31 downto 24); end if;
      end if;
    end if;
  end process;
  dout <= mem(i);
  top_bit <= mem(i)(31);

  process (clk)
  begin
    if rising_edge(clk) then
      if be(0) = '1' then lo_b(i) <= din(7 downto 0); end if;
      if be(1) = '1' then hi_b(i) <= din(15 downto 8); end if;
    end if;
  end process;
  pair <= hi_b(i) & lo_b(i);
end rtl;
