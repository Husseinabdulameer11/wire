title: NAND: two gates in one
table: yes
---- question
A **NAND** gate is an AND followed by a NOT: it gives 0 only when both inputs are 1.

Gates can be combined in one line. Use brackets so the AND happens first, then the NOT: `not (a and b)`.
---- hint
`y <= not (a and b);`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity nand_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end nand_gate;

architecture rtl of nand_gate is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity nand_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end nand_gate;

architecture rtl of nand_gate is
begin
    y <= not (a and b);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity nand_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end nand_gate;

architecture rtl of nand_gate is
begin
    y <= (not a) and b;
end rtl;
