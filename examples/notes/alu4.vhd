-- 4-bit ALU: op selects add, and, or, xor.
-- The adder is one bit wider to catch the carry;
-- the flags are carry (add only) and zero.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity alu4 is
  port(x, y   : in  std_logic_vector(3 downto 0);
       op     : in  std_logic_vector(1 downto 0);
       result : out std_logic_vector(3 downto 0);
       carry, zero : out std_logic);
end alu4;

architecture rtl of alu4 is
  signal sum : unsigned(4 downto 0);
  signal r   : std_logic_vector(3 downto 0);
begin
  sum <= unsigned('0' & x) + unsigned('0' & y);
  with op select
    r <= std_logic_vector(sum(3 downto 0)) when "00",
         x and y                           when "01",
         x or y                            when "10",
         x xor y                           when others;
  result <= r;
  carry  <= sum(4) when op = "00" else '0';
  zero   <= '1' when r = "0000" else '0';
end rtl;
