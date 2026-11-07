title: Reverse the bits
table: yes
---- question
Make `y` the bits of `a` in the opposite order: bit 0 of `a` becomes bit 3 of `y`, and so on. For `a = "0001"`, `y = "1000"`.

Single bits are joined into a bus with `&`; the leftmost becomes the top bit.
---- hint
`y <= a(0) & a(1) & a(2) & a(3);`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity rev4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end rev4;

architecture rtl of rev4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity rev4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end rev4;

architecture rtl of rev4 is
begin
    y <= a(0) & a(1) & a(2) & a(3);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity rev4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end rev4;

architecture rtl of rev4 is
begin
    y <= a(1) & a(0) & a(3) & a(2);
end rtl;
