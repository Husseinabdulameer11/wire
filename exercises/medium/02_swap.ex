title: Buses: swap the halves
---- question
A **bus** is a group of wires carried together. In VHDL it is a `std_logic_vector`: `a(7 downto 0)` is eight bits, `a(7)` the leftmost (most significant) and `a(0)` the rightmost.

You can take a piece of a bus, `a(3 downto 0)`, and join pieces with `&`.

Make `y` the byte `a` with its two halves swapped: the low four bits of `a` become the high four bits of `y`, and the other way round. For `a = 1100 0101`, `y = 0101 1100`.
---- hint
`y <= a(3 downto 0) & a(7 downto 4);`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity swap_nibbles is
  port(
    a : in  std_logic_vector(7 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end swap_nibbles;

architecture rtl of swap_nibbles is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity swap_nibbles is
  port(
    a : in  std_logic_vector(7 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end swap_nibbles;

architecture rtl of swap_nibbles is
begin
    y <= a(3 downto 0) & a(7 downto 4);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity swap_nibbles is
  port(
    a : in  std_logic_vector(7 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end swap_nibbles;

architecture rtl of swap_nibbles is
begin
    y <= a(7 downto 4) & a(3 downto 0);
end rtl;
