title: The full subtractor
table: yes
---- question
Like the full adder, but subtracting: `d` is `a` − `b` − `bin` for one column, where `bin` is the borrow coming in from the column on the right. `bout` is 1 when this column has to borrow from the next one.

`d` is 1 when an odd number of the three are 1. `bout` is 1 when `b` + `bin` is more than `a`.
---- hint
`d <= a xor b xor bin;` and `bout <= (not a and b) or (not a and bin) or (b and bin);`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity full_sub is
  port(
    a    : in  std_logic;
    b    : in  std_logic;
    bin  : in  std_logic;
    d    : out std_logic;
    bout : out std_logic
  );
end full_sub;

architecture rtl of full_sub is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity full_sub is
  port(
    a    : in  std_logic;
    b    : in  std_logic;
    bin  : in  std_logic;
    d    : out std_logic;
    bout : out std_logic
  );
end full_sub;

architecture rtl of full_sub is
begin
    d    <= a xor b xor bin;
    bout <= (not a and b) or (not a and bin) or (b and bin);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity full_sub is
  port(
    a    : in  std_logic;
    b    : in  std_logic;
    bin  : in  std_logic;
    d    : out std_logic;
    bout : out std_logic
  );
end full_sub;

architecture rtl of full_sub is
begin
    d    <= a xor b xor bin;
    bout <= (a and b) or (a and bin) or (b and bin);
end rtl;
