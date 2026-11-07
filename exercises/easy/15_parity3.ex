title: Odd parity
table: yes
---- question
`p` is 1 when an **odd** number of `a`, `b` and `c` are 1 (one or three of them). This is a **parity bit**: sent along with data, it shows when one bit went wrong.
---- hint
XOR flips at every 1: `p <= a xor b xor c;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity parity3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    p : out std_logic
  );
end parity3;

architecture rtl of parity3 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity parity3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    p : out std_logic
  );
end parity3;

architecture rtl of parity3 is
begin
    p <= a xor b xor c;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity parity3 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    p : out std_logic
  );
end parity3;

architecture rtl of parity3 is
begin
    p <= a or b or c;
end rtl;
