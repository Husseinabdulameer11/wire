title: How far apart?
---- question
`d` is the distance between the 4-bit numbers `a` and `b`: the bigger minus the smaller, never negative. For 3 and 9, and for 9 and 3, `d` is 6.
---- hint
Compare first: `d <= std_logic_vector(unsigned(a) - unsigned(b)) when unsigned(a) >= unsigned(b) else std_logic_vector(unsigned(b) - unsigned(a));`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity absdiff is
  port(
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    d : out std_logic_vector(3 downto 0)
  );
end absdiff;

architecture rtl of absdiff is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity absdiff is
  port(
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    d : out std_logic_vector(3 downto 0)
  );
end absdiff;

architecture rtl of absdiff is
begin
    d <= std_logic_vector(unsigned(a) - unsigned(b)) when unsigned(a) >= unsigned(b) else
         std_logic_vector(unsigned(b) - unsigned(a));
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity absdiff is
  port(
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    d : out std_logic_vector(3 downto 0)
  );
end absdiff;

architecture rtl of absdiff is
begin
    d <= std_logic_vector(unsigned(a) - unsigned(b));
end rtl;
