-- The basic gates on two inputs. VHDL has an operator
-- for each one; NOT a from a NAND or NOR with its
-- inputs tied shows why those two are universal.
library ieee;
use ieee.std_logic_1164.all;

entity gates is
  port(a, b : in  std_logic;
       y_and, y_or, y_nand, y_nor, y_xor, y_xnor : out std_logic;
       y_not, y_buf, nand_not, nor_not : out std_logic);
end gates;

architecture dataflow of gates is
begin
  y_and    <= a and b;
  y_or     <= a or b;
  y_nand   <= a nand b;
  y_nor    <= a nor b;
  y_xor    <= a xor b;
  y_xnor   <= a xnor b;
  y_not    <= not a;
  y_buf    <= a;
  nand_not <= a nand a;     -- = not a
  nor_not  <= a nor a;      -- = not a
end dataflow;
