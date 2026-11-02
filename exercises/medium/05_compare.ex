title: Comparing numbers
---- question
Compare two 4-bit unsigned numbers. `eq` is 1 when they are equal, `gt` is 1 when `a` is greater than `b`.

`when ... else` works with comparisons too: `eq <= '1' when a = b else '0';`. For greater than, compare them as numbers: `unsigned(a) > unsigned(b)`.
---- hint
```
eq <= '1' when a = b else '0';
gt <= '1' when unsigned(a) > unsigned(b) else '0';
```
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity compare4 is
  port(
    a  : in  std_logic_vector(3 downto 0);
    b  : in  std_logic_vector(3 downto 0);
    eq : out std_logic;
    gt : out std_logic
  );
end compare4;

architecture rtl of compare4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity compare4 is
  port(
    a  : in  std_logic_vector(3 downto 0);
    b  : in  std_logic_vector(3 downto 0);
    eq : out std_logic;
    gt : out std_logic
  );
end compare4;

architecture rtl of compare4 is
begin
    eq <= '1' when a = b else '0';
    gt <= '1' when unsigned(a) > unsigned(b) else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity compare4 is
  port(
    a  : in  std_logic_vector(3 downto 0);
    b  : in  std_logic_vector(3 downto 0);
    eq : out std_logic;
    gt : out std_logic
  );
end compare4;

architecture rtl of compare4 is
begin
    eq <= '1' when a = b else '0';
    gt <= '1' when unsigned(a) >= unsigned(b) else '0';
end rtl;
