library ieee;
use ieee.std_logic_1164.all;
entity tri_bus is
  port (a, b : in std_logic_vector(7 downto 0); ea, eb : in std_logic;
        y : out std_logic_vector(7 downto 0); q : out std_logic);
end tri_bus;
architecture rtl of tri_bus is
  signal d : std_logic_vector(7 downto 0);
begin
  d <= a when ea = '1' else (others => 'Z');
  d <= b when eb = '1' else "ZZZZZZZZ";
  y <= d;
  q <= 'Z' when ea = '0' else a(0);
end rtl;

-- an output driven by two chips
library ieee;
use ieee.std_logic_1164.all;
entity tri_buf is
  port (x : in std_logic_vector(3 downto 0); en : in std_logic; y : out std_logic_vector(3 downto 0));
end tri_buf;
architecture rtl of tri_buf is
begin
  y <= x when en = '1' else (others => 'Z');
end rtl;

library ieee;
use ieee.std_logic_1164.all;
entity tri_two is
  port (a, b : in std_logic_vector(7 downto 0); ea, eb : in std_logic;
        y : out std_logic_vector(7 downto 0); q : out std_logic; o : out std_logic_vector(3 downto 0));
end tri_two;
architecture rtl of tri_two is
begin
  t : entity work.tri_bus port map (a => a, b => b, ea => ea, eb => eb, y => y, q => q);
  u : entity work.tri_buf port map (x => a(3 downto 0), en => ea, y => o);
  v : entity work.tri_buf port map (x => b(3 downto 0), en => eb, y => o);
end rtl;
