title: Adding one, with overflow
table: yes
---- question
`y` is `a` + 1, in 4 bits. For `a = "1111"` the answer does not fit: `y` wraps round to `"0000"` and `ovf` is 1. For every other `a`, `ovf` is 0.

You can make the sum 5 bits wide, then split it.
---- hint
Use a 5-bit signal: `t <= resize(unsigned(a), 5) + 1;` then `y <= std_logic_vector(t(3 downto 0)); ovf <= t(4);`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity inc4 is
  port(
    a   : in  std_logic_vector(3 downto 0);
    y   : out std_logic_vector(3 downto 0);
    ovf : out std_logic
  );
end inc4;

architecture rtl of inc4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity inc4 is
  port(
    a   : in  std_logic_vector(3 downto 0);
    y   : out std_logic_vector(3 downto 0);
    ovf : out std_logic
  );
end inc4;

architecture rtl of inc4 is
    signal t : unsigned(4 downto 0);
begin
    t   <= resize(unsigned(a), 5) + 1;
    y   <= std_logic_vector(t(3 downto 0));
    ovf <= t(4);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity inc4 is
  port(
    a   : in  std_logic_vector(3 downto 0);
    y   : out std_logic_vector(3 downto 0);
    ovf : out std_logic
  );
end inc4;

architecture rtl of inc4 is
    signal t : unsigned(4 downto 0);
begin
    t   <= resize(unsigned(a), 5) + 1;
    y   <= std_logic_vector(t(3 downto 0));
    ovf <= '0';
end rtl;
