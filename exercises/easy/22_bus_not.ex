title: Your first bus: invert four bits
table: yes
---- question
A **bus** is a group of wires: `std_logic_vector(3 downto 0)` is four bits, numbered 3 (left) down to 0. A gate on a bus works on every bit at once.

Make `y` the opposite of `a`, bit by bit: for `a = "0011"`, `y = "1100"`.
---- hint
It looks like a single NOT gate: `y <= not a;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity inv4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end inv4;

architecture rtl of inv4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity inv4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end inv4;

architecture rtl of inv4 is
begin
    y <= not a;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity inv4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end inv4;

architecture rtl of inv4 is
begin
    y <= a;
end rtl;
