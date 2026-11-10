-- a package of types, constants and functions (with its body), used by two
-- entities; another package builds on it
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package cpu_pkg is
  constant WORD : integer := 8;
  subtype word_t is std_logic_vector(WORD - 1 downto 0);
  type op_t is (op_add, op_sub, op_and, op_pass);
  type bank_t is array (0 to 3) of word_t;
  constant ZERO : word_t := (others => '0');
  function to_op(code : std_logic_vector(1 downto 0)) return op_t;
  function alu(op : op_t; a, b : word_t) return word_t;
end package cpu_pkg;

package body cpu_pkg is
  function to_op(code : std_logic_vector(1 downto 0)) return op_t is
  begin
    case code is
      when "00" => return op_add;
      when "01" => return op_sub;
      when "10" => return op_and;
      when others => return op_pass;
    end case;
  end function;
  function alu(op : op_t; a, b : word_t) return word_t is
  begin
    case op is
      when op_add => return std_logic_vector(unsigned(a) + unsigned(b));
      when op_sub => return std_logic_vector(unsigned(a) - unsigned(b));
      when op_and => return a and b;
      when op_pass => return b;
    end case;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.cpu_pkg.all;
package more_pkg is
  constant ONES : word_t := (others => '1');
  function inv(x : word_t) return word_t;
end package;
package body more_pkg is
  function inv(x : word_t) return word_t is
  begin
    return x xor ONES;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cpu_pkg.all;
entity regbank is
  port (clk, we : in std_logic; wa, ra : in std_logic_vector(1 downto 0); d : in word_t; q : out word_t);
end regbank;
architecture rtl of regbank is
  signal bank : bank_t := (others => ZERO);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if we = '1' then bank(to_integer(unsigned(wa))) <= d; end if;
    end if;
  end process;
  q <= bank(to_integer(unsigned(ra)));
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use work.cpu_pkg.all;
use work.more_pkg.all;
entity packages is
  port (clk, we : in std_logic; wa, ra, code : in std_logic_vector(1 downto 0);
        d : in std_logic_vector(7 downto 0);
        y, n : out std_logic_vector(7 downto 0));
end packages;
architecture rtl of packages is
  signal r : word_t;
begin
  b : entity work.regbank port map (clk => clk, we => we, wa => wa, ra => ra, d => d, q => r);
  y <= alu(to_op(code), r, d);
  n <= inv(r);
end rtl;
