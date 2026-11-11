-- An electronic die (Basys-3): hold run and it rolls, let go and it stops;
-- with cheat it stops on r (1 to 6). Its tables of constants (with select)
-- and comparisons with constants are read as gates, and its registers reset
-- to 1 with init = (tests/vhdl/cosim.js --vhdl-in checks it against GHDL).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity edice is
  port(clk   : in  std_logic;
       rst   : in  std_logic;
       run   : in  std_logic;
       cheat : in  std_logic;
       r     : in  std_logic_vector(2 downto 0);
       seg   : out std_logic_vector(6 downto 0);
       dots  : out std_logic_vector(6 downto 0));
end edice;

architecture rtl of edice is
  signal roll  : unsigned(2 downto 0);
  signal value : unsigned(2 downto 0);
  signal run_d : std_logic;
begin
  process(clk, rst)
  begin
    if rst = '1' then
      roll  <= "001";
      value <= "001";
      run_d <= '0';
    elsif rising_edge(clk) then
      run_d <= run;
      if roll = 6 then
        roll <= "001";
      else
        roll <= roll + 1;
      end if;
      if run = '1' then
        value <= roll;
      elsif run_d = '1' and cheat = '1'
            and unsigned(r) >= 1 and unsigned(r) <= 6 then
        value <= unsigned(r);
      end if;
    end if;
  end process;

  with value select
    seg <= "0000110" when "001",
           "1011011" when "010",
           "1001111" when "011",
           "1100110" when "100",
           "1101101" when "101",
           "1111101" when "110",
           "0000000" when others;

  with value select
    dots <= "0001000" when "001",
            "1000001" when "010",
            "1001001" when "011",
            "1100011" when "100",
            "1101011" when "101",
            "1110111" when "110",
            "0000000" when others;
end rtl;

library ieee;
use ieee.std_logic_1164.all;

entity dice_top is
  port(clk   : in  std_logic;
       rst   : in  std_logic;
       run   : in  std_logic;
       cheat : in  std_logic;
       r     : in  std_logic_vector(2 downto 0);
       seg   : out std_logic_vector(6 downto 0);
       dots  : out std_logic_vector(6 downto 0));
end dice_top;

architecture structural of dice_top is
begin
  die: entity work.edice
    port map(clk => clk, rst => rst, run => run, cheat => cheat,
             r => r, seg => seg, dots => dots);
end structural;
