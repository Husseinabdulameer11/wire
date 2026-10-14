-- Register file, 2^W words of B bits, one write and one
-- read port (Chu, Listing 4.6), here W = 2 and B = 8.
-- The array has no reset; the initial value is only for
-- the simulator (Wire's flip-flops also start at 0).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity reg_file is
  generic(B : integer := 8;     -- bits per word
          W : integer := 2);    -- address bits
  port(clk    : in  std_logic;
       wr_en  : in  std_logic;
       w_addr, r_addr : in  std_logic_vector(W-1 downto 0);
       w_data : in  std_logic_vector(B-1 downto 0);
       r_data : out std_logic_vector(B-1 downto 0));
end reg_file;

architecture arch of reg_file is
  type reg_file_type is array (0 to 2**W-1) of
       std_logic_vector(B-1 downto 0);
  signal array_reg : reg_file_type := (others => (others => '0'));
begin
  -- write port: one register loads at the clock edge
  process(clk)
  begin
    if rising_edge(clk) then
      if wr_en = '1' then
        array_reg(to_integer(unsigned(w_addr))) <= w_data;
      end if;
    end if;
  end process;
  -- read port: a mux, no clock
  r_data <= array_reg(to_integer(unsigned(r_addr)));
end arch;
