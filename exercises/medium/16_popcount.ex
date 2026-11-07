title: Counting the ones
table: yes
---- question
`n` is how many bits of `a` are 1, from 0 to 4 (3 bits). For `a = "1011"`, `n` is 3.

Each bit is a 0 or a 1, so the count is their sum. Make each bit a 3-bit number before adding: `"00" & a(0)`.
---- hint
`n <= std_logic_vector(unsigned("00" & a(0)) + unsigned("00" & a(1)) + unsigned("00" & a(2)) + unsigned("00" & a(3)));`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity popcount4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    n : out std_logic_vector(2 downto 0)
  );
end popcount4;

architecture rtl of popcount4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity popcount4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    n : out std_logic_vector(2 downto 0)
  );
end popcount4;

architecture rtl of popcount4 is
begin
    n <= std_logic_vector(unsigned("00" & a(0)) + unsigned("00" & a(1)) + unsigned("00" & a(2)) + unsigned("00" & a(3)));
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity popcount4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    n : out std_logic_vector(2 downto 0)
  );
end popcount4;

architecture rtl of popcount4 is
begin
    n <= std_logic_vector(unsigned("00" & a(0)) + unsigned("00" & a(1)) + unsigned("00" & a(2)));
end rtl;
