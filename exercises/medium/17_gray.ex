title: Binary to Gray code
table: yes
---- question
In **Gray code**, counting up changes only one bit at a time, which makes it safe for rotary encoders and for passing a count between clocks. Each Gray bit is the binary bit XOR the bit to its left; the top bit stays the same.

For `b = "0110"` (6), `g = "0101"`.
---- hint
`g <= b xor ('0' & b(3 downto 1));`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bin2gray is
  port(
    b : in  std_logic_vector(3 downto 0);
    g : out std_logic_vector(3 downto 0)
  );
end bin2gray;

architecture rtl of bin2gray is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bin2gray is
  port(
    b : in  std_logic_vector(3 downto 0);
    g : out std_logic_vector(3 downto 0)
  );
end bin2gray;

architecture rtl of bin2gray is
begin
    g <= b xor ('0' & b(3 downto 1));
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bin2gray is
  port(
    b : in  std_logic_vector(3 downto 0);
    g : out std_logic_vector(3 downto 0)
  );
end bin2gray;

architecture rtl of bin2gray is
begin
    g <= b xor (b(2 downto 0) & '0');
end rtl;
