title: Rotating a byte
---- question
Rotate the byte `a` one place to the left: every bit moves up one, and the top bit comes round to bit 0. For `a = "10000011"`, `y = "00000111"`.
---- hint
Slice and join: `y <= a(6 downto 0) & a(7);`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rotl8 is
  port(
    a : in  std_logic_vector(7 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end rotl8;

architecture rtl of rotl8 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rotl8 is
  port(
    a : in  std_logic_vector(7 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end rotl8;

architecture rtl of rotl8 is
begin
    y <= a(6 downto 0) & a(7);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rotl8 is
  port(
    a : in  std_logic_vector(7 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end rotl8;

architecture rtl of rotl8 is
begin
    y <= a(6 downto 0) & '0';
end rtl;
