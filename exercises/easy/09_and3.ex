title: AND with three inputs
table: yes
---- question
`y` is 1 only when `a`, `b` **and** `c` are all 1. Seven of the eight rows give 0.
---- hint
Operators chain: `y <= a and b and c;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity and3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end and3;

architecture rtl of and3 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity and3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end and3;

architecture rtl of and3 is
begin
    y <= a and b and c;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity and3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end and3;

architecture rtl of and3 is
begin
    y <= (a and b) or c;
end rtl;
