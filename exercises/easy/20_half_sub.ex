title: Subtracting two bits
---- question
Subtract `b` from `a`, one bit each. `d` is the answer bit and `borrow` is 1 when you had to borrow, which happens only for 0 − 1.

| a | b | borrow | d |
|---|---|---|---|
| 0 | 0 | 0 | 0 |
| 0 | 1 | 1 | 1 |
| 1 | 0 | 0 | 1 |
| 1 | 1 | 0 | 0 |
---- hint
`d` is XOR; `borrow` is `not a and b`.
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity half_sub is
  port(
    a      : in  std_logic;
    b      : in  std_logic;
    d      : out std_logic;
    borrow : out std_logic
  );
end half_sub;

architecture rtl of half_sub is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity half_sub is
  port(
    a      : in  std_logic;
    b      : in  std_logic;
    d      : out std_logic;
    borrow : out std_logic
  );
end half_sub;

architecture rtl of half_sub is
begin
    d      <= a xor b;
    borrow <= not a and b;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity half_sub is
  port(
    a      : in  std_logic;
    b      : in  std_logic;
    d      : out std_logic;
    borrow : out std_logic
  );
end half_sub;

architecture rtl of half_sub is
begin
    d      <= a xor b;
    borrow <= a and not b;
end rtl;
