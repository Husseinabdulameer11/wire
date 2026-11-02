title: A 4-bit adder
---- question
Add two 4-bit numbers `a` and `b`. The result can be up to 15 + 15 = 30, so `s` has **5** bits: the fifth is the carry out.

You could chain four full adders, but VHDL can do arithmetic for you. The `numeric_std` library (already in the starter) has `unsigned` numbers that you can add with `+`:

- `unsigned(a)` treats the bits of `a` as a number
- `resize(x, 5)` makes a number 5 bits wide
- `std_logic_vector(x)` turns a number back into bits for the output
---- hint
Make `a` 5 bits wide first, so the carry is not lost: `s <= std_logic_vector(resize(unsigned(a), 5) + unsigned(b));`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity adder4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    s : out std_logic_vector(4 downto 0)
  );
end adder4;

architecture rtl of adder4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity adder4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    s : out std_logic_vector(4 downto 0)
  );
end adder4;

architecture rtl of adder4 is
begin
    s <= std_logic_vector(resize(unsigned(a), 5) + unsigned(b));
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity adder4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    s : out std_logic_vector(4 downto 0)
  );
end adder4;

architecture rtl of adder4 is
begin
    s <= '0' & std_logic_vector(unsigned(a) + unsigned(b));
end rtl;
