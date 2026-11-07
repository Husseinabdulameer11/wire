title: A barrel shifter
---- question
Shift the byte `a` left by `s` places (0 to 7), with zeros coming in on the right. For `a = "00000111"` and `s = "011"` (3), `y = "00111000"`.

A **barrel shifter** does any shift in one step, and every processor has one for its shift instructions. Try it two ways: with `numeric_std`'s `shift_left`, or as three stages that shift by 1, 2 and 4 depending on each bit of `s`.
---- hint
`y <= std_logic_vector(shift_left(unsigned(a), to_integer(unsigned(s))));`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity barrel8 is
  port(
    a : in  std_logic_vector(7 downto 0);
    s : in  std_logic_vector(2 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end barrel8;

architecture rtl of barrel8 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity barrel8 is
  port(
    a : in  std_logic_vector(7 downto 0);
    s : in  std_logic_vector(2 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end barrel8;

architecture rtl of barrel8 is
begin
    y <= std_logic_vector(shift_left(unsigned(a), to_integer(unsigned(s))));
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity barrel8 is
  port(
    a : in  std_logic_vector(7 downto 0);
    s : in  std_logic_vector(2 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end barrel8;

architecture rtl of barrel8 is
begin
    y <= std_logic_vector(rotate_left(unsigned(a), to_integer(unsigned(s))));
end rtl;
