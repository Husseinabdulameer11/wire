-- Hex digit to 7-segment pattern (Chu, Listing 3.12)
library ieee;
use ieee.std_logic_1164.all;

entity hex_to_sseg is
  port(h    : in  std_logic_vector(3 downto 0);
       dp   : in  std_logic;
       sseg : out std_logic_vector(7 downto 0));
end hex_to_sseg;

architecture arch of hex_to_sseg is
begin
  with h select
    sseg(6 downto 0) <=
      "0000001" when "0000", "1001111" when "0001",
      "0010010" when "0010", "0000110" when "0011",
      "1001100" when "0100", "0100100" when "0101",
      "0100000" when "0110", "0001111" when "0111",
      "0000000" when "1000", "0000100" when "1001",
      "0001000" when "1010", "1100000" when "1011",
      "0110001" when "1100", "1000010" when "1101",
      "0110000" when "1110", "0111000" when others;
  sseg(7) <= dp;
end arch;

-- Time-multiplexed hex display (Chu, Listing 4.15):
-- one decoder, shared by the four digits in turn.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity disp_hex_mux is
  port(clk, reset : in  std_logic;
       hex3, hex2, hex1, hex0 : in  std_logic_vector(3 downto 0);
       dp_in : in  std_logic_vector(3 downto 0);
       an    : out std_logic_vector(3 downto 0);
       sseg  : out std_logic_vector(7 downto 0));
end disp_hex_mux;

architecture arch of disp_hex_mux is
  constant N : integer := 4;            -- 18 on the board
  signal q_reg : unsigned(N-1 downto 0);
  signal sel   : std_logic_vector(1 downto 0);
  signal h     : std_logic_vector(3 downto 0);
  signal dp    : std_logic;
begin
  process(clk, reset)
  begin
    if reset = '1' then
      q_reg <= (others => '0');
    elsif rising_edge(clk) then
      q_reg <= q_reg + 1;
    end if;
  end process;
  sel <= std_logic_vector(q_reg(N-1 downto N-2));
  process(sel, hex0, hex1, hex2, hex3, dp_in)
  begin
    case sel is
      when "00"   => an <= "1110"; h <= hex0; dp <= dp_in(0);
      when "01"   => an <= "1101"; h <= hex1; dp <= dp_in(1);
      when "10"   => an <= "1011"; h <= hex2; dp <= dp_in(2);
      when others => an <= "0111"; h <= hex3; dp <= dp_in(3);
    end case;
  end process;
  dec: entity work.hex_to_sseg port map(h => h, dp => dp, sseg => sseg);
end arch;
