title: Adding two bits
table: yes
---- question
Adding two single bits gives a number from 0 to 2, which needs two bits to write: a **sum** bit and a **carry** bit.

- 0 + 0 = 0: sum 0, carry 0
- 0 + 1 = 1: sum 1, carry 0
- 1 + 1 = 2 (10 in binary): sum 0, carry 1

This part has two outputs, so you write two lines. Look at the table: which gate gives the sum, and which gives the carry?
---- hint
The sum is 1 when the inputs are different (that is XOR). The carry is 1 only when both are 1 (that is AND).
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity half_adder is
  port(
    a     : in  std_logic;
    b     : in  std_logic;
    sum   : out std_logic;
    carry : out std_logic
  );
end half_adder;

architecture rtl of half_adder is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity half_adder is
  port(
    a     : in  std_logic;
    b     : in  std_logic;
    sum   : out std_logic;
    carry : out std_logic
  );
end half_adder;

architecture rtl of half_adder is
begin
    sum   <= a xor b;
    carry <= a and b;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity half_adder is
  port(
    a     : in  std_logic;
    b     : in  std_logic;
    sum   : out std_logic;
    carry : out std_logic
  );
end half_adder;

architecture rtl of half_adder is
begin
    sum   <= a or b;
    carry <= a and b;
end rtl;
