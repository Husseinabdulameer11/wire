title: Choosing between two inputs
table: yes
---- question
A **multiplexer** (mux) is a switch: the select input `s` chooses which input goes to the output. When `s` is 0, `y` is `a`; when `s` is 1, `y` is `b`.

VHDL can say this almost in words: `y <= a when s = '0' else b;`

Single bits are written in single quotes: `'0'` and `'1'`.
---- hint
`y <= a when s = '0' else b;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity mux2 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    s : in  std_logic;
    y : out std_logic
  );
end mux2;

architecture rtl of mux2 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity mux2 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    s : in  std_logic;
    y : out std_logic
  );
end mux2;

architecture rtl of mux2 is
begin
    y <= a when s = '0' else b;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity mux2 is
  port(
    a : in  std_logic;
    b : in  std_logic;
    s : in  std_logic;
    y : out std_logic
  );
end mux2;

architecture rtl of mux2 is
begin
    y <= b when s = '0' else a;
end rtl;
