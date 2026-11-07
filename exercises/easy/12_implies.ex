title: If A then B
---- question
In logic, "if `a` then `b`" is only broken when `a` is true and `b` is false. `y` is 0 in that one row and 1 in all the others.

| a | b | y |
|---|---|---|
| 0 | 0 | 1 |
| 0 | 1 | 1 |
| 1 | 0 | 0 |
| 1 | 1 | 1 |
---- hint
It is 1 when `a` is 0, or when `b` is 1: `y <= not a or b;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity implies is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end implies;

architecture rtl of implies is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity implies is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end implies;

architecture rtl of implies is
begin
    y <= not a or b;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity implies is
  port(
    a : in  std_logic;
    b : in  std_logic;
    y : out std_logic
  );
end implies;

architecture rtl of implies is
begin
    y <= a or not b;
end rtl;
