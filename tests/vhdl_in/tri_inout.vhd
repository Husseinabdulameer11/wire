-- a shared data bus: a register file and a memory chip both have an inout
-- data port, each drives it while its output enable is on; the top's data
-- port is inout too, so the page (or a testbench) can drive it as well
-- In the test exactly one driver talks at a time: two at once are a conflict
-- (X), and a register that loads a bus nobody drives keeps 'Z' in VHDL but 0
-- in Wire, so the testbench drives the bus whenever no chip does.
-- test: apart oe_a oe_b oe_n data_oe
-- test: apart oe_a flag_oe
-- test: data_oe=1 flag_oe=1
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity bus_reg is
  port (clk, ld, oe : in std_logic;
        data : inout std_logic_vector(7 downto 0));
end bus_reg;
architecture rtl of bus_reg is
  signal r : std_logic_vector(7 downto 0) := (others => '0');
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if ld = '1' then r <= data; end if;
    end if;
  end process;
  data <= r when oe = '1' else (others => 'Z');
end rtl;

library ieee;
use ieee.std_logic_1164.all;
entity bus_not is
  port (oe : in std_logic; data : inout std_logic_vector(7 downto 0); seen : out std_logic_vector(7 downto 0));
end bus_not;
architecture rtl of bus_not is
begin
  seen <= data;
  data <= "10100101" when oe = '1' else (others => 'Z');
end rtl;

library ieee;
use ieee.std_logic_1164.all;
entity tri_inout is
  port (clk : in std_logic;
        ld_a, oe_a, ld_b, oe_b, oe_n : in std_logic;
        data : inout std_logic_vector(7 downto 0);
        seen : out std_logic_vector(7 downto 0);
        flag : inout std_logic);
end tri_inout;
architecture rtl of tri_inout is
  signal inner : std_logic_vector(7 downto 0);
begin
  a : entity work.bus_reg port map (clk => clk, ld => ld_a, oe => oe_a, data => data);
  b : entity work.bus_reg port map (clk => clk, ld => ld_b, oe => oe_b, data => data);
  n : entity work.bus_not port map (oe => oe_n, data => data, seen => inner);
  seen <= inner;
  flag <= data(0) when oe_a = '1' else 'Z';
end rtl;
