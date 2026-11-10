-- generics that are booleans, bits and vectors (with generic maps), a case
-- on a generic, choices that are ranges, and a case on an integer that can
-- be negative
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity gen_unit is
  generic (INVERT : boolean := false; FILL : std_logic := '0';
           KEY : std_logic_vector(7 downto 0) := x"5A"; MODE : integer := 0);
  port (x : in std_logic_vector(7 downto 0); y : out std_logic_vector(7 downto 0); f : out std_logic);
end gen_unit;
architecture rtl of gen_unit is
begin
  g_inv : if INVERT generate
    y <= not (x xor KEY);
  end generate;
  g_plain : if not INVERT generate
    y <= x xor KEY;
  end generate;
  process (x)
  begin
    case MODE is
      when 0 => f <= FILL;
      when 1 to 3 => f <= x(MODE);
      when others => f <= KEY(0);
    end case;
  end process;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity generics2 is
  port (x : in std_logic_vector(7 downto 0);
        s : in std_logic_vector(3 downto 0);
        y1, y2 : out std_logic_vector(7 downto 0);
        f1, f2 : out std_logic;
        band : out std_logic_vector(1 downto 0);
        sgn : out std_logic_vector(1 downto 0));
end generics2;
architecture rtl of generics2 is
  signal v : integer range -8 to 7;
begin
  a : entity work.gen_unit port map (x => x, y => y1, f => f1);
  b : entity work.gen_unit generic map (INVERT => true, FILL => '1', KEY => x"C3", MODE => 2) port map (x => x, y => y2, f => f2);
  with to_integer(unsigned(s)) select
    band <= "00" when 0 to 3,
            "01" when 4 to 7 | 9,
            "10" when 8 | 10 to 12,
            "11" when others;
  v <= to_integer(signed(s));
  process (v)
  begin
    case v is
      when -8 to -1 => sgn <= "10";
      when 0 => sgn <= "00";
      when others => sgn <= "01";
    end case;
  end process;
end rtl;
