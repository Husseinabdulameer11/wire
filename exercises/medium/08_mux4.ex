title: Choosing one of four
---- question
The 2-bit select `s` picks one of the four bits of `d` to send to `y`: `s = "00"` gives `d(0)`, up to `s = "11"`, which gives `d(3)`.
---- hint
`with s select y <= d(0) when "00", d(1) when "01", d(2) when "10", d(3) when others;`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mux4 is
  port(
    d : in  std_logic_vector(3 downto 0);
    s : in  std_logic_vector(1 downto 0);
    y : out std_logic
  );
end mux4;

architecture rtl of mux4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mux4 is
  port(
    d : in  std_logic_vector(3 downto 0);
    s : in  std_logic_vector(1 downto 0);
    y : out std_logic
  );
end mux4;

architecture rtl of mux4 is
begin
    with s select y <=
        d(0) when "00",
        d(1) when "01",
        d(2) when "10",
        d(3) when others;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mux4 is
  port(
    d : in  std_logic_vector(3 downto 0);
    s : in  std_logic_vector(1 downto 0);
    y : out std_logic
  );
end mux4;

architecture rtl of mux4 is
begin
    with s select y <=
        d(0) when "00",
        d(2) when "01",
        d(1) when "10",
        d(3) when others;
end rtl;
