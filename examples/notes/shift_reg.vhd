-- 4-bit serial-in, parallel-out shift register.
-- At each rising edge every bit moves one place up
-- and din comes in at bit 0.
library ieee;
use ieee.std_logic_1164.all;

entity shift_reg is
  port(clk, clr : in  std_logic;
       din      : in  std_logic;
       q        : out std_logic_vector(3 downto 0));
end shift_reg;

architecture rtl of shift_reg is
  signal r : std_logic_vector(3 downto 0);
begin
  process(clk, clr)
  begin
    if clr = '1' then
      r <= (others => '0');
    elsif rising_edge(clk) then
      r <= r(2 downto 0) & din;
    end if;
  end process;
  q <= r;
end rtl;
