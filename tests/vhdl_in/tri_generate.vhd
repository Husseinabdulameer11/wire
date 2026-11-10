-- four tri-state drivers made by a for generate share one bus; one hot
-- select picks who talks (none: the bus floats)
-- test: apart e0 e1 e2 e3
library ieee;
use ieee.std_logic_1164.all;
entity tri_generate is
  port (x0, x1, x2, x3 : in std_logic_vector(7 downto 0);
        e0, e1, e2, e3 : in std_logic;
        y : out std_logic_vector(7 downto 0));
end tri_generate;
architecture rtl of tri_generate is
  type word_arr is array (0 to 3) of std_logic_vector(7 downto 0);
  signal x : word_arr;
  signal en : std_logic_vector(3 downto 0);
  signal d : std_logic_vector(7 downto 0);
begin
  x <= (x0, x1, x2, x3);
  en <= e3 & e2 & e1 & e0;
  drivers : for i in 0 to 3 generate
    d <= x(i) when en(i) = '1' else (others => 'Z');
  end generate;
  y <= d;
end rtl;
