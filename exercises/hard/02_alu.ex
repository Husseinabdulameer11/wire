title: An ALU with a zero flag
---- question
An 8-operation 4-bit ALU. `y` is:

| op | y |
|----|---|
| 000 | `a and b` |
| 001 | `a or b` |
| 010 | `a xor b` |
| 011 | `a + b` (wraps round) |
| 100 | `a - b` (wraps round) |
| 101 | `not a` |
| 110 | `a` shifted left one place, a 0 coming in |
| 111 | `a` shifted right one place, a 0 coming in |

`z` (the **zero flag**) is 1 when `y` is `"0000"`. Processors test it to decide whether to jump.

An output cannot be read, so compute the result into a signal first.
---- hint
`with op select r <= a and b when "000", ..., a(2 downto 0) & '0' when "110", '0' & a(3 downto 1) when others;` then `y <= r;` and `z <= '1' when r = "0000" else '0';`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity alu4 is
  port(
    a  : in  std_logic_vector(3 downto 0);
    b  : in  std_logic_vector(3 downto 0);
    op : in  std_logic_vector(2 downto 0);
    y  : out std_logic_vector(3 downto 0);
    z  : out std_logic
  );
end alu4;

architecture rtl of alu4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity alu4 is
  port(
    a  : in  std_logic_vector(3 downto 0);
    b  : in  std_logic_vector(3 downto 0);
    op : in  std_logic_vector(2 downto 0);
    y  : out std_logic_vector(3 downto 0);
    z  : out std_logic
  );
end alu4;

architecture rtl of alu4 is
    signal r : std_logic_vector(3 downto 0);
begin
    with op select r <=
        a and b when "000",
        a or b  when "001",
        a xor b when "010",
        std_logic_vector(unsigned(a) + unsigned(b)) when "011",
        std_logic_vector(unsigned(a) - unsigned(b)) when "100",
        not a   when "101",
        a(2 downto 0) & '0' when "110",
        '0' & a(3 downto 1) when others;
    y <= r;
    z <= '1' when r = "0000" else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity alu4 is
  port(
    a  : in  std_logic_vector(3 downto 0);
    b  : in  std_logic_vector(3 downto 0);
    op : in  std_logic_vector(2 downto 0);
    y  : out std_logic_vector(3 downto 0);
    z  : out std_logic
  );
end alu4;

architecture rtl of alu4 is
    signal r : std_logic_vector(3 downto 0);
begin
    with op select r <=
        a and b when "000",
        a or b  when "001",
        a xor b when "010",
        std_logic_vector(unsigned(a) + unsigned(b)) when "011",
        std_logic_vector(unsigned(b) - unsigned(a)) when "100",
        not a   when "101",
        a(2 downto 0) & '0' when "110",
        '0' & a(3 downto 1) when others;
    y <= r;
    z <= '1' when r = "0000" else '0';
end rtl;
