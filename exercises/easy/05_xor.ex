title: Exclusive OR
table: yes
---- question
**XOR** (exclusive or) gives 1 when the inputs are **different**: one of them is 1, but not both. It is the gate that tells you whether two bits disagree.

The VHDL operator is `xor`.
---- hint
`y <= a xor b;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity xor_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end xor_gate;

architecture rtl of xor_gate is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity xor_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end xor_gate;

architecture rtl of xor_gate is
begin
    y <= a xor b;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity xor_gate is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end xor_gate;

architecture rtl of xor_gate is
begin
    y <= a or b;
end rtl;
