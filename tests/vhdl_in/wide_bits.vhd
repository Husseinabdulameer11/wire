-- a 64-bit register whose bits are set and read with a signal index
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity wide_bits is
  port (clk, set, clr : in std_logic; i, j : in std_logic_vector(5 downto 0);
        bit_i, bit_j : out std_logic; lo, hi : out std_logic_vector(31 downto 0));
end wide_bits;
architecture rtl of wide_bits is
  signal r : std_logic_vector(63 downto 0) := (others => '0');
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if set = '1' then r(to_integer(unsigned(i))) <= '1'; end if;
      if clr = '1' then r(to_integer(unsigned(j))) <= '0'; end if;
    end if;
  end process;
  bit_i <= r(to_integer(unsigned(i)));
  bit_j <= r(to_integer(unsigned(j)));
  lo <= r(31 downto 0);
  hi <= r(63 downto 32);
end rtl;
