title: OR with three inputs
table: yes
---- question
An **OR gate** gives 1 when **at least one** of its inputs is 1. This one has three inputs: `y` is 0 only when `a`, `b` and `c` are all 0.

You can chain operators: `a or b or c`.
---- hint
`y <= a or b or c;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity or3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end or3;

architecture rtl of or3 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity or3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end or3;

architecture rtl of or3 is
begin
    y <= a or b or c;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity or3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end or3;

architecture rtl of or3 is
begin
    y <= a or b;
end rtl;
