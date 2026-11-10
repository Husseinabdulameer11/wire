library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity subtypes is
  port (ir : in std_logic_vector(7 downto 0); y : out std_logic_vector(3 downto 0); b : out std_logic;
        z : out std_logic_vector(7 downto 0));
end subtypes;
architecture rtl of subtypes is
  subtype nibble is std_logic_vector(3 downto 0);
  subtype idx is integer range 0 to 7;
  type small is range 0 to 9;
  alias op : nibble is ir(7 downto 4);
  alias lo is ir(3 downto 0);
  signal t : std_logic_vector(7 downto 0);
  alias th : nibble is t(7 downto 4);
  alias tl is t(3 downto 0);
  constant K : idx := 5;
begin
  y <= op xor lo;
  b <= op(3) and lo(0) and ir(K);
  th <= lo;
  tl <= op;
  z <= t;
end rtl;
