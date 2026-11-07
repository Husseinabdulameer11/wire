title: A priority encoder
table: yes
---- question
Four requests come in on `r`. `n` is the number of the **highest** one that is 1, and `valid` is 1 when there is any request at all. With no request, `n` is `"00"` and `valid` is 0.

For `r = "0110"`, requests 1 and 2 are on, and the highest is 2: `n = "10"`.
---- hint
A `when ... else` chain, highest first: `n <= "11" when r(3) = '1' else "10" when r(2) = '1' else ...`; `valid <= '1' when r /= "0000" else '0';`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity prio4 is
  port(
    r     : in  std_logic_vector(3 downto 0);
    n     : out std_logic_vector(1 downto 0);
    valid : out std_logic
  );
end prio4;

architecture rtl of prio4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity prio4 is
  port(
    r     : in  std_logic_vector(3 downto 0);
    n     : out std_logic_vector(1 downto 0);
    valid : out std_logic
  );
end prio4;

architecture rtl of prio4 is
begin
    n <= "11" when r(3) = '1' else
         "10" when r(2) = '1' else
         "01" when r(1) = '1' else
         "00";
    valid <= '1' when r /= "0000" else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity prio4 is
  port(
    r     : in  std_logic_vector(3 downto 0);
    n     : out std_logic_vector(1 downto 0);
    valid : out std_logic
  );
end prio4;

architecture rtl of prio4 is
begin
    n <= "00" when r(0) = '1' else
         "01" when r(1) = '1' else
         "10" when r(2) = '1' else
         "11";
    valid <= '1' when r /= "0000" else '0';
end rtl;
