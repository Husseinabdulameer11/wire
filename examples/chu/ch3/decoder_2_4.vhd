-- 2-to-4 binary decoder with enable (Chu, Listings 3.2,
-- 3.4, 3.6, 3.8). Here as a case statement in a process;
-- all four styles give the same four AND gates.
library ieee;
use ieee.std_logic_1164.all;

entity decoder_2_4 is
  port(a  : in  std_logic_vector(1 downto 0);
       en : in  std_logic;
       y  : out std_logic_vector(3 downto 0));
end decoder_2_4;

architecture case_arch of decoder_2_4 is
begin
  process(en, a)
  begin
    y <= "0000";                  -- default: no line
    if en = '1' then
      case a is
        when "00"   => y <= "0001";
        when "01"   => y <= "0010";
        when "10"   => y <= "0100";
        when others => y <= "1000";
      end case;
    end if;
  end process;
end case_arch;
