title: The full adder
table: yes
---- question
A **full adder** adds three bits: `a`, `b` and a carry coming in, `cin`, from the column to the right. It is the building block of every adder: put eight in a row and you can add 8-bit numbers.

The result is 0 to 3: a sum bit `s` and a carry out `cout`.
---- hint
`s` is 1 when an odd number of inputs are 1: `a xor b xor cin`. `cout` is 1 when at least two are 1: `(a and b) or (a and cin) or (b and cin)`.
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity full_adder is
  port(
    a    : in  std_logic;
    b    : in  std_logic;
    cin  : in  std_logic;
    s    : out std_logic;
    cout : out std_logic
  );
end full_adder;

architecture rtl of full_adder is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity full_adder is
  port(
    a    : in  std_logic;
    b    : in  std_logic;
    cin  : in  std_logic;
    s    : out std_logic;
    cout : out std_logic
  );
end full_adder;

architecture rtl of full_adder is
begin
    s    <= a xor b xor cin;
    cout <= (a and b) or (a and cin) or (b and cin);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity full_adder is
  port(
    a    : in  std_logic;
    b    : in  std_logic;
    cin  : in  std_logic;
    s    : out std_logic;
    cout : out std_logic
  );
end full_adder;

architecture rtl of full_adder is
begin
    s    <= a xor b xor cin;
    cout <= (a and b) or (b and cin);
end rtl;
