title: Parity of a byte
---- question
`p` is 1 when the byte `a` has an **odd** number of 1 bits. Memory chips store a bit like this with every byte to spot a bit that flipped.
---- hint
XOR all eight bits together: `p <= a(0) xor a(1) xor ... xor a(7);`
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity parity8 is
  port(
    a : in  std_logic_vector(7 downto 0);
    p : out std_logic
  );
end parity8;

architecture rtl of parity8 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity parity8 is
  port(
    a : in  std_logic_vector(7 downto 0);
    p : out std_logic
  );
end parity8;

architecture rtl of parity8 is
begin
    p <= a(0) xor a(1) xor a(2) xor a(3) xor a(4) xor a(5) xor a(6) xor a(7);
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity parity8 is
  port(
    a : in  std_logic_vector(7 downto 0);
    p : out std_logic
  );
end parity8;

architecture rtl of parity8 is
begin
    p <= a(0) xor a(1) xor a(2) xor a(3) xor a(4) xor a(5) xor a(6);
end rtl;
