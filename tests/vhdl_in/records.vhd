-- records: as signals, registers (reset as a whole), constants, aggregates,
-- in when/else, as the ports of a chip; and ports given in parts
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package rec_pkg is
  type flags_t is record
    z, n, c : std_logic;
  end record;
  type alu_out_t is record
    y : std_logic_vector(7 downto 0);
    f : flags_t;
  end record;
  type cmd_t is record
    op : std_logic_vector(1 downto 0);
    a, b : std_logic_vector(7 downto 0);
  end record;
  constant NOFLAGS : flags_t := (others => '0');
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.rec_pkg.all;
entity rec_alu is
  port (cmd : in cmd_t; res : out alu_out_t);
end rec_alu;
architecture rtl of rec_alu is
  signal sum : unsigned(8 downto 0);
begin
  sum <= resize(unsigned(cmd.a), 9) + resize(unsigned(cmd.b), 9);
  process (cmd, sum)
    variable r : alu_out_t;
  begin
    r := (y => (others => '0'), f => NOFLAGS);
    case cmd.op is
      when "00" => r.y := std_logic_vector(sum(7 downto 0)); r.f.c := sum(8);
      when "01" => r.y := cmd.a and cmd.b;
      when "10" => r.y := cmd.a or cmd.b;
      when others => r.y := not cmd.a;
    end case;
    if r.y = x"00" then r.f.z := '1'; end if;
    r.f.n := r.y(7);
    res <= r;
  end process;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.rec_pkg.all;
entity records is
  port (clk, rst, ld : in std_logic;
        op : in std_logic_vector(1 downto 0);
        a, b : in std_logic_vector(7 downto 0);
        y : out std_logic_vector(7 downto 0);
        z, n, c : out std_logic;
        hi_lo : out std_logic_vector(7 downto 0));
end records;
architecture rtl of records is
  signal cmd : cmd_t;
  signal res : alu_out_t;
  signal saved : flags_t;
  signal sw : std_logic_vector(7 downto 0);
  signal unused : flags_t;
begin
  cmd <= (op => op, a => a, b => b) when ld = '1' else (op => "11", a => b, b => a);
  u : entity work.rec_alu port map (cmd => cmd, res => res);
  y <= res.y;
  process (clk, rst)
  begin
    if rst = '1' then
      saved <= NOFLAGS;
    elsif rising_edge(clk) then
      if ld = '1' then saved <= res.f; end if;
    end if;
  end process;
  z <= saved.z; n <= saved.n; c <= saved.c;
  -- the halves of a swapped by a chip whose ports are given in parts
  v : entity work.rec_alu port map (cmd.op => "10", cmd.a(3 downto 0) => a(7 downto 4), cmd.a(7 downto 4) => a(3 downto 0),
                                    cmd.b => x"00", res.y => sw, res.f => unused);
  hi_lo <= sw;
end rtl;
