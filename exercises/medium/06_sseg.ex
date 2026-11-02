title: A hex digit on a 7-segment display
table: yes
---- question
A **7-segment display** shows a digit with seven lit bars, called a to g. Here `seg` is 7 bits, `seg(6 downto 0) = g f e d c b a`, and a 1 lights a bar.

```
 aaa
f   b
 ggg
e   c
 ddd
```

Show the hex digit `h` (0 to F). The table lists the segments for every digit; a `with h select` statement with sixteen lines is the usual way to write it.
---- hint
Each row of the table becomes one line: `"0111111" when "0000",` for 0, `"0000110" when "0001",` for 1, and so on, ending with `"1110001" when others;` for F.
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity hex_sseg is
  port(
    h   : in  std_logic_vector(3 downto 0);
    seg : out std_logic_vector(6 downto 0)
  );
end hex_sseg;

architecture rtl of hex_sseg is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity hex_sseg is
  port(
    h   : in  std_logic_vector(3 downto 0);
    seg : out std_logic_vector(6 downto 0)
  );
end hex_sseg;

architecture rtl of hex_sseg is
begin
    with h select seg <=
        "0111111" when "0000",
        "0000110" when "0001",
        "1011011" when "0010",
        "1001111" when "0011",
        "1100110" when "0100",
        "1101101" when "0101",
        "1111101" when "0110",
        "0000111" when "0111",
        "1111111" when "1000",
        "1101111" when "1001",
        "1110111" when "1010",
        "1111100" when "1011",
        "0111001" when "1100",
        "1011110" when "1101",
        "1111001" when "1110",
        "1110001" when others;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity hex_sseg is
  port(
    h   : in  std_logic_vector(3 downto 0);
    seg : out std_logic_vector(6 downto 0)
  );
end hex_sseg;

architecture rtl of hex_sseg is
begin
    with h select seg <=
        "0111111" when "0000",
        "0000110" when "0001",
        "1011011" when "0010",
        "1001111" when "0011",
        "1100110" when "0100",
        "1101101" when "0101",
        "1111101" when "0110",
        "0000111" when "0111",
        "1111111" when "1000",
        "1101111" when "1001",
        "0000000" when others;
end rtl;
