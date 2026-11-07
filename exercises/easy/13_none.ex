title: All of them off
table: yes
---- question
A "room is empty" light: `empty` is 1 only when none of the three motion sensors `a`, `b` and `c` sees anything (all three are 0).
---- hint
That is NOT of an OR: `empty <= not (a or b or c);`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity empty is
  port(
    a     : in  std_logic;
    b     : in  std_logic;
    c     : in  std_logic;
    empty : out std_logic
  );
end empty;

architecture rtl of empty is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity empty is
  port(
    a     : in  std_logic;
    b     : in  std_logic;
    c     : in  std_logic;
    empty : out std_logic
  );
end empty;

architecture rtl of empty is
begin
    empty <= not (a or b or c);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity empty is
  port(
    a     : in  std_logic;
    b     : in  std_logic;
    c     : in  std_logic;
    empty : out std_logic
  );
end empty;

architecture rtl of empty is
begin
    empty <= not (a and b and c);
end rtl;
