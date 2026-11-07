title: A UART transmitter
cycles: 700
random: rst=0.002 start=0.08
---- question
A **UART** sends a byte over one wire, a bit at a time. The line `tx` is 1 while nothing is sent. To send a byte, the transmitter puts out, in order:

1. a **start bit**: 0
2. the 8 **data bits**, `data(0)` first (least significant first)
3. a **stop bit**: 1

Every bit lasts exactly **4 clock cycles** here (a real UART uses many more, set by the baud rate).

- When `start` is 1 at a clock edge and the transmitter is not busy, it takes the byte on `data`, `busy` becomes 1, and from that edge `tx` is the start bit.
- 4 cycles per bit, 10 bits: 40 clock edges after the start, the stop bit is done, and `busy` goes back to 0. `tx` is then 1 (idle) again.
- `start` is ignored while `busy` is 1, and `rst` stops everything: idle, `tx` = 1, `busy` = 0.

`tx` and `busy` change only on clock edges.
---- hint
One way: keep a 10-bit frame `'1' & data & '0'` in a shift register when starting, and make `tx` its lowest bit. Count clock cycles 0 to 3; at 3, shift the frame right (a 1 comes in at the top) and count bits; after the 10th bit, stop being busy.
---- starter
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity uart_tx is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    start : in  std_logic;
    data : in  std_logic_vector(7 downto 0);
    tx   : out std_logic;
    busy : out std_logic
  );
end uart_tx;

architecture rtl of uart_tx is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity uart_tx is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    start : in  std_logic;
    data : in  std_logic_vector(7 downto 0);
    tx   : out std_logic;
    busy : out std_logic
  );
end uart_tx;

architecture rtl of uart_tx is
    signal frame  : std_logic_vector(9 downto 0) := (others => '1');
    signal cyc    : unsigned(1 downto 0) := (others => '0');
    signal bits   : unsigned(3 downto 0) := (others => '0');
    signal busy_r : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                busy_r <= '0';
                frame  <= (others => '1');
                cyc    <= (others => '0');
                bits   <= (others => '0');
            elsif busy_r = '0' then
                if start = '1' then
                    busy_r <= '1';
                    frame  <= '1' & data & '0';
                    cyc    <= (others => '0');
                    bits   <= (others => '0');
                end if;
            else
                if cyc = 3 then
                    cyc <= (others => '0');
                    if bits = 9 then
                        busy_r <= '0';
                        frame  <= (others => '1');
                    else
                        bits  <= bits + 1;
                        frame <= '1' & frame(9 downto 1);
                    end if;
                else
                    cyc <= cyc + 1;
                end if;
            end if;
        end if;
    end process;
    tx   <= frame(0);
    busy <= busy_r;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity uart_tx is
  port(
    clk  : in  std_logic;
    rst  : in  std_logic;
    start : in  std_logic;
    data : in  std_logic_vector(7 downto 0);
    tx   : out std_logic;
    busy : out std_logic
  );
end uart_tx;

architecture rtl of uart_tx is
    signal frame  : std_logic_vector(9 downto 0) := (others => '1');
    signal cyc    : unsigned(1 downto 0) := (others => '0');
    signal bits   : unsigned(3 downto 0) := (others => '0');
    signal busy_r : std_logic := '0';
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                busy_r <= '0';
                frame  <= (others => '1');
                cyc    <= (others => '0');
                bits   <= (others => '0');
            elsif busy_r = '0' then
                if start = '1' then
                    busy_r <= '1';
                    frame  <= '1' & data & '0';
                    cyc    <= (others => '0');
                    bits   <= (others => '0');
                end if;
            else
                if cyc = 3 then
                    cyc <= (others => '0');
                    if bits = 8 then
                        busy_r <= '0';
                        frame  <= (others => '1');
                    else
                        bits  <= bits + 1;
                        frame <= '1' & frame(9 downto 1);
                    end if;
                else
                    cyc <= cyc + 1;
                end if;
            end if;
        end if;
    end process;
    tx   <= frame(0);
    busy <= busy_r;
end rtl;
