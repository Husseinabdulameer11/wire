-- a ram written in a process that does more: a registered read (the
-- usual block-ram template), a counter, another array, and a reset for
-- everything but the ram
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity ram_proc is
  port (clk, rst, we : in std_logic;
        addr : in std_logic_vector(4 downto 0);
        din : in std_logic_vector(7 downto 0);
        dout : out std_logic_vector(7 downto 0);
        cnt : out std_logic_vector(3 downto 0);
        last : out std_logic_vector(7 downto 0));
end ram_proc;
architecture rtl of ram_proc is
  type mem_t is array (0 to 31) of std_logic_vector(7 downto 0);
  type hist_t is array (0 to 1) of std_logic_vector(7 downto 0);
  signal mem : mem_t := (others => (others => '0'));
  signal hist : hist_t;
  signal c : unsigned(3 downto 0);
begin
  process (clk, rst)
  begin
    if rst = '1' then
      c <= (others => '0');
      dout <= (others => '0');
      hist <= (others => (others => '0'));
    elsif rising_edge(clk) then
      if we = '1' then
        mem(to_integer(unsigned(addr))) <= din;
        c <= c + 1;
        hist(1) <= hist(0);
        hist(0) <= din;
      end if;
      dout <= mem(to_integer(unsigned(addr)));
    end if;
  end process;
  cnt <= std_logic_vector(c);
  last <= hist(1);
end rtl;
