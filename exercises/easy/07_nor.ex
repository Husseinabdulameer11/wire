title: NOR: neither one
table: yes
---- question
A **NOR gate** is an OR followed by a NOT. `y` is 1 only when **neither** `a` nor `b` is 1.

Use brackets so the OR happens first: `not (a or b)`.
---- hint
`y <= not (a or b);`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity nor_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end nor_gate;

architecture rtl of nor_gate is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity nor_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end nor_gate;

architecture rtl of nor_gate is
begin
    y <= not (a or b);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity nor_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end nor_gate;

architecture rtl of nor_gate is
begin
    y <= not a or b;
end rtl;
