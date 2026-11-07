title: Masking bits
---- question
An AND on a bus is a **mask**: where `m` has a 1, the bit of `a` goes through; where `m` has a 0, `y` has a 0.

For `a = "1011"` and `m = "0110"`, `y = "0010"`.
---- hint
`y <= a and m;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity mask4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    m : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end mask4;

architecture rtl of mask4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity mask4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    m : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end mask4;

architecture rtl of mask4 is
begin
    y <= a and m;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity mask4 is
  port(
    a : in  std_logic_vector(3 downto 0);
    m : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end mask4;

architecture rtl of mask4 is
begin
    y <= a or m;
end rtl;
