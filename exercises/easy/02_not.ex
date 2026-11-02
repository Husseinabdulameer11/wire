title: The NOT gate
table: yes
---- question
A **NOT gate** (an inverter) turns a 1 into a 0 and a 0 into a 1. The table shows what it must do for every input.

In VHDL the operator is called `not`: `y <= not a;`
---- hint
Use the same shape as the wire, with `not` in front of `a`.
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity inverter is
  port(
    a : in  std_logic;
    y : out std_logic
  );
end inverter;

architecture rtl of inverter is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity inverter is
  port(
    a : in  std_logic;
    y : out std_logic
  );
end inverter;

architecture rtl of inverter is
begin
    y <= not a;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity inverter is
  port(
    a : in  std_logic;
    y : out std_logic
  );
end inverter;

architecture rtl of inverter is
begin
    y <= a;
end rtl;
