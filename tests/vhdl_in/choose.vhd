-- with/select (choices joined with |, and too wide for one mux) and
-- when/else chains with a boolean signal.
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity choose is
  port(op   : in  std_logic_vector(2 downto 0);
       code : in  std_logic_vector(5 downto 0);
       a, b : in  std_logic_vector(3 downto 0);
       f    : out std_logic_vector(3 downto 0);
       cls  : out std_logic_vector(1 downto 0);
       big  : out std_logic;
       pick : out std_logic_vector(3 downto 0));
end choose;

architecture rtl of choose is
  signal agtb : boolean;
begin
  with op select
    f <= a + b      when "000",
         a - b      when "001",
         a and b    when "010" | "011",
         not a      when "100",
         a + 1      when others;
  with code select
    cls <= "01" when "000000",
           "10" when "111111" | "101010",
           "11" when "000111",
           "00" when others;
  agtb <= a > b;
  big  <= '1' when agtb else '0';
  pick <= a when agtb and op(0) = '1' else
          b when op(1) = '1' else
          "1111";
end rtl;
