title: A signal and its opposite
table: yes
---- question
Many chips need a signal and its opposite side by side. Make `q` follow `a`, and `nq` be the opposite of `a`, at the same time.
---- hint
Two lines: `q <= a;` and `nq <= not a;`
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity both is
  port(
    a  : in  std_logic;
    q  : out std_logic;
    nq : out std_logic
  );
end both;

architecture rtl of both is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity both is
  port(
    a  : in  std_logic;
    q  : out std_logic;
    nq : out std_logic
  );
end both;

architecture rtl of both is
begin
    q  <= a;
    nq <= not a;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity both is
  port(
    a  : in  std_logic;
    q  : out std_logic;
    nq : out std_logic
  );
end both;

architecture rtl of both is
begin
    q  <= a;
    nq <= a;
end rtl;
