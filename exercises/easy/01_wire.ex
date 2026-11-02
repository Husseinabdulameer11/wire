title: A wire
table: yes
---- question
Welcome! Digital circuits are made of signals that are either **0** or **1**, and parts that work on them. In VHDL you describe a circuit in two pieces:

- the **entity** is the outside of the part: its pins. Here it has one input, `a`, and one output, `y`. It is already written for you.
- the **architecture** is the inside: what the part does. You write that.

Your first circuit is just a wire: `y` should always be the same as `a`. In VHDL, `<=` means "is connected to", so `y <= a;` makes `y` follow `a`.

Write that line where it says `-- your code here`, then press **Check**.
---- hint
The whole answer is one line: `y <= a;` (with the semicolon).
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity pass_through is
  port(
    a : in  std_logic;
    y : out std_logic
  );
end pass_through;

architecture rtl of pass_through is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity pass_through is
  port(
    a : in  std_logic;
    y : out std_logic
  );
end pass_through;

architecture rtl of pass_through is
begin
    y <= a;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity pass_through is
  port(
    a : in  std_logic;
    y : out std_logic
  );
end pass_through;

architecture rtl of pass_through is
begin
    y <= '0';
end rtl;
