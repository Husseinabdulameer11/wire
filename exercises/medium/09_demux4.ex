title: Sending one input to one of four
---- question
A **demultiplexer**: the bit `d` goes to the output `y(n)` that `s` points at, and every other bit of `y` is 0. For `d = '1'` and `s = "10"`, `y = "0100"`.
---- hint
Every bit of `y` is `d` when `s` is its number: `y(2) <= d when s = "10" else '0';` and so on for each bit.
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity demux4 is
  port(
    d : in  std_logic;
    s : in  std_logic_vector(1 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end demux4;

architecture rtl of demux4 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity demux4 is
  port(
    d : in  std_logic;
    s : in  std_logic_vector(1 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end demux4;

architecture rtl of demux4 is
begin
    y(0) <= d when s = "00" else '0';
    y(1) <= d when s = "01" else '0';
    y(2) <= d when s = "10" else '0';
    y(3) <= d when s = "11" else '0';
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity demux4 is
  port(
    d : in  std_logic;
    s : in  std_logic_vector(1 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end demux4;

architecture rtl of demux4 is
begin
    y(0) <= d when s = "00" else '0';
    y(1) <= d when s = "01" else '0';
    y(2) <= d when s = "10" else '0';
    y(3) <= '1' when s = "11" else '0';
end rtl;
