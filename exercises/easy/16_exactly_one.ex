title: Exactly one of three
table: yes
---- question
`y` is 1 when **exactly one** of `a`, `b` and `c` is 1: not none, not two, not three.

Careful: `a xor b xor c` is also 1 when all three are 1.
---- hint
Odd parity, but not all three: `y <= (a xor b xor c) and not (a and b and c);`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity one_of3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end one_of3;

architecture rtl of one_of3 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity one_of3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end one_of3;

architecture rtl of one_of3 is
begin
    y <= (a xor b xor c) and not (a and b and c);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity one_of3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end one_of3;

architecture rtl of one_of3 is
begin
    y <= a xor b xor c;
end rtl;
