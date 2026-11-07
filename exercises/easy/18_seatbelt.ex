title: The seat belt warning
table: yes
---- question
A car's warning light `warn` comes on when the `engine` is running and someone is not belted in:

- the driver always sits there: warn if `dbelt` is 0
- the passenger seat only counts when someone sits there (`pseat` is 1) and `pbelt` is 0

Break it into pieces before writing it as one line.
---- hint
`warn <= engine and (not dbelt or (pseat and not pbelt));`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity seatbelt is
  port(
    engine : in  std_logic;
    dbelt  : in  std_logic;
    pseat  : in  std_logic;
    pbelt  : in  std_logic;
    warn   : out std_logic
  );
end seatbelt;

architecture rtl of seatbelt is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity seatbelt is
  port(
    engine : in  std_logic;
    dbelt  : in  std_logic;
    pseat  : in  std_logic;
    pbelt  : in  std_logic;
    warn   : out std_logic
  );
end seatbelt;

architecture rtl of seatbelt is
begin
    warn <= engine and (not dbelt or (pseat and not pbelt));
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity seatbelt is
  port(
    engine : in  std_logic;
    dbelt  : in  std_logic;
    pseat  : in  std_logic;
    pbelt  : in  std_logic;
    warn   : out std_logic
  );
end seatbelt;

architecture rtl of seatbelt is
begin
    warn <= engine and (not dbelt or not pbelt);
end rtl;
