title: The bigger of two
---- question
`m` is whichever of the 4-bit numbers `a` and `b` is bigger (either one if they are equal).
---- hint
`m <= a when unsigned(a) > unsigned(b) else b;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity max4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    m : out std_logic_vector(3 downto 0)
  );
end max4;

architecture rtl of max4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity max4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    m : out std_logic_vector(3 downto 0)
  );
end max4;

architecture rtl of max4 is
begin
    m <= a when unsigned(a) > unsigned(b) else b;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity max4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    m : out std_logic_vector(3 downto 0)
  );
end max4;

architecture rtl of max4 is
begin
    m <= a when a(3) = '1' else b;
end rtl;
