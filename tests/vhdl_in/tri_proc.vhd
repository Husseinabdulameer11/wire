library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity tri_proc is
  port (a, b : in std_logic_vector(3 downto 0); sel : in std_logic_vector(1 downto 0); oe : in std_logic;
        y, w : out std_logic_vector(3 downto 0); vh : out std_logic_vector(31 downto 0); vl : out std_logic_vector(7 downto 0));
end tri_proc;
architecture rtl of tri_proc is
  signal v : std_logic_vector(39 downto 0);
begin
  process (a, b, sel, oe)
  begin
    y <= (others => 'Z');
    if oe = '1' then
      case sel is
        when "00" => y <= a;
        when "01" => y <= b;
        when "10" => y <= a and b;
        when others => null;
      end case;
    end if;
  end process;
  with sel select w <= a when "00", "ZZZZ" when "01", b when others;
  v <= a & b & a & b & a & b & a & b & b & a when oe = '0' else (others => 'Z');
  vh <= v(39 downto 8);
  vl <= v(7 downto 0);
end rtl;
