title: Finding 1101, without overlap
cycles: 500
random: rst=0.005 x=0.6
---- question
One bit arrives on `x` at every rising edge. `found` is 1 for the cycle after the fourth bit of a **1 1 0 1** arrived (oldest bit first).

Unlike the 101 finder, matches may **not** overlap: once 1101 is found, those four bits are used up, and the next match needs four new bits. In 1 1 0 1 1 0 1, the pattern is found once, not twice.

`rst` forgets every bit received so far.
---- hint
Keep the last bits and how many there are since the last match: on each edge, with `w = last(2 downto 0) & x`, if there are already 3 bits and `w = "1101"`, set `found` and forget the bits; else `found <= '0'`, keep `w` and count one more bit (up to 3).
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity seq1101 is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    x     : in  std_logic;
    found : out std_logic
  );
end seq1101;

architecture rtl of seq1101 is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity seq1101 is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    x     : in  std_logic;
    found : out std_logic
  );
end seq1101;

architecture rtl of seq1101 is
    signal last, w : std_logic_vector(3 downto 0) := (others => '0');
    signal n : unsigned(1 downto 0) := (others => '0');
    signal f : std_logic := '0';
begin
    w <= last(2 downto 0) & x;
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                last <= (others => '0');
                n    <= (others => '0');
                f    <= '0';
            elsif n = 3 and w = "1101" then
                f    <= '1';
                last <= (others => '0');
                n    <= (others => '0');
            else
                f    <= '0';
                last <= w;
                if n /= 3 then
                    n <= n + 1;
                end if;
            end if;
        end if;
    end process;
    found <= f;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity seq1101 is
  port(
    clk   : in  std_logic;
    rst   : in  std_logic;
    x     : in  std_logic;
    found : out std_logic
  );
end seq1101;

architecture rtl of seq1101 is
    signal last, w : std_logic_vector(3 downto 0) := (others => '0');
    signal n : unsigned(1 downto 0) := (others => '0');
    signal f : std_logic := '0';
begin
    w <= last(2 downto 0) & x;
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                last <= (others => '0');
                n    <= (others => '0');
                f    <= '0';
            elsif n = 3 and w = "1101" then
                f    <= '1';
                last <= w;
            else
                f    <= '0';
                last <= w;
                if n /= 3 then
                    n <= n + 1;
                end if;
            end if;
        end if;
    end process;
    found <= f;
end rtl;
