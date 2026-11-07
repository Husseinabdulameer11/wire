title: A burglar alarm
table: yes
---- question
The siren `y` sounds when the alarm is `armed` **and** the `door` opens, or at any time when the `panic` button is pressed.
---- hint
`y <= (armed and door) or panic;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity alarm is
  port(
    armed : in  std_logic;
    door  : in  std_logic;
    panic : in  std_logic;
    y     : out std_logic
  );
end alarm;

architecture rtl of alarm is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity alarm is
  port(
    armed : in  std_logic;
    door  : in  std_logic;
    panic : in  std_logic;
    y     : out std_logic
  );
end alarm;

architecture rtl of alarm is
begin
    y <= (armed and door) or panic;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity alarm is
  port(
    armed : in  std_logic;
    door  : in  std_logic;
    panic : in  std_logic;
    y     : out std_logic
  );
end alarm;

architecture rtl of alarm is
begin
    y <= armed and (door or panic);
end rtl;
