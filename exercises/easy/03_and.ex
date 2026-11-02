title: The AND gate
table: yes
---- question
An **AND gate** has two inputs. Its output is 1 only when **both** inputs are 1; otherwise it is 0. Look at the table: only the last row gives 1.

The VHDL operator is `and`: `y <= a and b;`
---- hint
`y <= a and b;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity and_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end and_gate;

architecture rtl of and_gate is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity and_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end and_gate;

architecture rtl of and_gate is
begin
    y <= a and b;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity and_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end and_gate;

architecture rtl of and_gate is
begin
    y <= a or b;
end rtl;
