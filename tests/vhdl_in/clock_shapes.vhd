-- clocked processes written other ways: logic and variables beside the
-- clock edge (process (all)), two edge blocks on one clock, a reset with
-- else null, and processes that wait for the edge
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity clock_shapes is
  port (clk, rst, en : in std_logic;
        a, b : in std_logic_vector(7 downto 0);
        q, s, w1, w2 : out std_logic_vector(7 downto 0);
        both : out std_logic_vector(3 downto 0));
end clock_shapes;
architecture rtl of clock_shapes is
  signal r1, r2 : unsigned(7 downto 0) := (others => '0');
  signal c1, c2 : unsigned(1 downto 0) := "00";
  signal ww1, ww2 : std_logic_vector(7 downto 0) := (others => '0');
begin
  -- a variable worked out before the edge, a sum beside it
  process (all)
    variable t : unsigned(7 downto 0);
  begin
    t := unsigned(a) xor unsigned(b);
    if rst = '1' then
      r1 <= (others => '0');
    elsif rising_edge(clk) then
      if en = '1' then r1 <= t; end if;
    else
      null;
    end if;
    s <= std_logic_vector(unsigned(a) + unsigned(b));
  end process;
  q <= std_logic_vector(r1);

  -- two edge blocks, one clock
  process (clk)
  begin
    if rising_edge(clk) then
      c1 <= c1 + 1;
    end if;
    if rising_edge(clk) then
      if en = '1' then c2 <= c2 + 1; end if;
    end if;
  end process;
  both <= std_logic_vector(c2 & c1);

  -- waiting for the edge
  process
  begin
    wait until rising_edge(clk);
    ww1 <= a;
  end process;
  process
  begin
    if en = '1' then ww2 <= b; end if;
    wait until clk = '1';
  end process;
  w1 <= ww1;
  w2 <= ww2;
end rtl;
