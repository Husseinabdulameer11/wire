title: Majority vote
table: yes
---- question
Three sensors vote: `y` is 1 when **at least two** of `a`, `b` and `c` are 1.

Each pair being 1 is one AND; any of the three pairs is enough.
---- hint
`y <= (a and b) or (a and c) or (b and c);`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity majority is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end majority;

architecture rtl of majority is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity majority is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end majority;

architecture rtl of majority is
begin
    y <= (a and b) or (a and c) or (b and c);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity majority is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end majority;

architecture rtl of majority is
begin
    y <= (a and b) or (b and c);
end rtl;
