title: A 2-to-4 decoder
---- question
A **decoder** turns a binary number into one wire per value: the input `a` (2 bits, 0 to 3) lights exactly one of the four outputs `y(0)` to `y(3)`, but only when the enable `en` is 1. When `en` is 0, all outputs are 0.

| en | a  | y    |
|----|----|------|
| 0  | -- | 0000 |
| 1  | 00 | 0001 |
| 1  | 01 | 0010 |
| 1  | 10 | 0100 |
| 1  | 11 | 1000 |

A `with ... select` statement chooses a value for each case: put `en` and `a` together with `&` and select on the 3-bit result.
---- hint
```
with en & a select y <=
    "0001" when "100",
    "0010" when "101",
    "0100" when "110",
    "1000" when "111",
    "0000" when others;
```
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity decoder_2_4 is
  port(
    en : in  std_logic;
    a  : in  std_logic_vector(1 downto 0);
    y  : out std_logic_vector(3 downto 0)
  );
end decoder_2_4;

architecture rtl of decoder_2_4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity decoder_2_4 is
  port(
    en : in  std_logic;
    a  : in  std_logic_vector(1 downto 0);
    y  : out std_logic_vector(3 downto 0)
  );
end decoder_2_4;

architecture rtl of decoder_2_4 is
begin
    with en & a select y <=
        "0001" when "100",
        "0010" when "101",
        "0100" when "110",
        "1000" when "111",
        "0000" when others;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity decoder_2_4 is
  port(
    en : in  std_logic;
    a  : in  std_logic_vector(1 downto 0);
    y  : out std_logic_vector(3 downto 0)
  );
end decoder_2_4;

architecture rtl of decoder_2_4 is
begin
    with a select y <=
        "0001" when "00",
        "0010" when "01",
        "0100" when "10",
        "1000" when others;
end rtl;
