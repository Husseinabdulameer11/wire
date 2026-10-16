-- expect: with ... select must cover every value of s ("11" is missing); add when others
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_cover is
  port(s : in std_logic_vector(1 downto 0); y : out std_logic);
end vhdl_cover;

architecture rtl of vhdl_cover is
begin
  with s select
    y <= '0' when "00",
         '1' when "01" | "10";
end rtl;
