title: A, but not B
table: yes
---- question
A motor `run` turns when the `start` button is held, but never while the safety door is open (`door` = 1).

`run` is 1 when `start` is 1 **and** `door` is 0.
---- hint
`run <= start and not door;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity inhibit is
  port(
    start : in  std_logic;
    door  : in  std_logic;
    run   : out std_logic
  );
end inhibit;

architecture rtl of inhibit is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity inhibit is
  port(
    start : in  std_logic;
    door  : in  std_logic;
    run   : out std_logic
  );
end inhibit;

architecture rtl of inhibit is
begin
    run <= start and not door;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity inhibit is
  port(
    start : in  std_logic;
    door  : in  std_logic;
    run   : out std_logic
  );
end inhibit;

architecture rtl of inhibit is
begin
    run <= start xor door;
end rtl;
