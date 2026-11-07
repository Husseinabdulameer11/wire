title: Subtracting, with a borrow flag
---- question
`d` is `a` − `b`, in 4 bits (it wraps round when `b` is bigger). `borrow` is 1 exactly when `b` is bigger than `a`.
---- hint
`d <= std_logic_vector(unsigned(a) - unsigned(b));` and `borrow <= '1' when unsigned(b) > unsigned(a) else '0';`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sub4 is
  port(
    a      : in  std_logic_vector(3 downto 0);
    b      : in  std_logic_vector(3 downto 0);
    d      : out std_logic_vector(3 downto 0);
    borrow : out std_logic
  );
end sub4;

architecture rtl of sub4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sub4 is
  port(
    a      : in  std_logic_vector(3 downto 0);
    b      : in  std_logic_vector(3 downto 0);
    d      : out std_logic_vector(3 downto 0);
    borrow : out std_logic
  );
end sub4;

architecture rtl of sub4 is
begin
    d      <= std_logic_vector(unsigned(a) - unsigned(b));
    borrow <= '1' when unsigned(b) > unsigned(a) else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sub4 is
  port(
    a      : in  std_logic_vector(3 downto 0);
    b      : in  std_logic_vector(3 downto 0);
    d      : out std_logic_vector(3 downto 0);
    borrow : out std_logic
  );
end sub4;

architecture rtl of sub4 is
begin
    d      <= std_logic_vector(unsigned(a) - unsigned(b));
    borrow <= '1' when unsigned(b) >= unsigned(a) else '0';
end rtl;
