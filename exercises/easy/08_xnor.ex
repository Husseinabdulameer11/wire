title: Are they equal?
table: yes
---- question
`eq` is 1 when the bits `a` and `b` have the **same** value, and 0 when they differ. This gate is called **XNOR**: an XOR turned round.
---- hint
XOR says "different": `eq <= not (a xor b);`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity eq1 is
  port(
    a  : in  std_logic;
    b  : in  std_logic;
    eq : out std_logic
  );
end eq1;

architecture rtl of eq1 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity eq1 is
  port(
    a  : in  std_logic;
    b  : in  std_logic;
    eq : out std_logic
  );
end eq1;

architecture rtl of eq1 is
begin
    eq <= not (a xor b);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity eq1 is
  port(
    a  : in  std_logic;
    b  : in  std_logic;
    eq : out std_logic
  );
end eq1;

architecture rtl of eq1 is
begin
    eq <= a and b;
end rtl;
