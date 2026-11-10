-- registered tri-state outputs: the value and its output enable are both
-- registers, and a reset can leave the output floating
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity tri_reg is
  port (clk, rst, oe, ld : in std_logic;
        d : in std_logic_vector(7 downto 0);
        q : out std_logic_vector(7 downto 0);
        b : out std_logic;
        n : out std_logic_vector(3 downto 0));
end tri_reg;
architecture rtl of tri_reg is
  signal cnt : unsigned(3 downto 0) := (others => '0');
begin
  process (clk, rst)
  begin
    if rst = '1' then
      q <= (others => 'Z');
      b <= '0';
    elsif rising_edge(clk) then
      if ld = '1' then
        if oe = '1' then q <= d; else q <= (others => 'Z'); end if;
      end if;
      if oe = '0' then b <= 'Z'; elsif ld = '1' then b <= d(0); end if;
    end if;
  end process;
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then cnt <= (others => '0'); else cnt <= cnt + 1; end if;
    end if;
  end process;
  n <= std_logic_vector(cnt) when oe = '1' else "ZZZZ";
end rtl;
