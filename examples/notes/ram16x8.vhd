-- 16 x 8 RAM, program preloaded (the "switches").
-- Read is combinational, like the 74189 chip:
-- address in, data out after the access time.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ram16x8 is
  port(addr : in  std_logic_vector(3 downto 0);
       dout : out std_logic_vector(7 downto 0));
end ram16x8;

architecture rtl of ram16x8 is
  type ram_t is array (0 to 15) of
       std_logic_vector(7 downto 0);
  constant RAM : ram_t := (
    0  => x"09",  -- LDA 9H
    1  => x"1A",  -- ADD AH
    2  => x"1B",  -- ADD BH
    3  => x"2C",  -- SUB CH
    4  => x"E0",  -- OUT
    5  => x"F0",  -- HLT
    9  => x"10",  -- 16
    10 => x"14",  -- 20
    11 => x"18",  -- 24
    12 => x"20",  -- 32
    others => x"00");
begin
  dout <= RAM(to_integer(unsigned(addr)));
end rtl;
