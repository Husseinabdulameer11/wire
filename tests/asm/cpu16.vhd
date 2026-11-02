-- cpu16.vhd - a 16-bit CPU with a 64 x 64 colour screen.
--
-- Single cycle: each clock runs one instruction from the program rom.
-- Eight 16-bit registers r0-r7, a 256-word program rom, a 256-word data
-- ram, a 64 x 64 screen of 8-bit pixels (RRRGGGBB) and a 16-bit display.
-- There is no wire_cpu attribute here: Wire has to find the CPU itself.
--
-- Instructions (16 bits; cpu16.isa describes them for the assembler):
--   0 ALU  rd, rs, rt   f: add sub and or xor shl shr slt
--   1 ADDI rd, rs, imm6         2 LI  rd, imm9      3 LUI rd, imm8
--   4 LD   rd, imm6(rs)         5 ST  rd, imm6(rs)
--   6 BEQ  rd, rs, off6         7 BNE rd, rs, off6  (pc + off)
--   8 JMP  addr                 A PIX rd, rs  (pixel rs = colour rd)
--   B OUT  rd                   F HLT
--
-- The program in the rom paints the screen with the xor pattern, shows
-- 4096 (0x1000) on the display and stops.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity alu16 is
  port(a, b : in  std_logic_vector(15 downto 0);
       f    : in  std_logic_vector(2 downto 0);
       y    : out std_logic_vector(15 downto 0));
end alu16;

architecture rtl of alu16 is
  signal shamt : integer range 0 to 15;
begin
  shamt <= to_integer(unsigned(b(3 downto 0)));
  process (a, b, f, shamt)
  begin
    case f is
      when "000" => y <= std_logic_vector(unsigned(a) + unsigned(b));
      when "001" => y <= std_logic_vector(unsigned(a) - unsigned(b));
      when "010" => y <= a and b;
      when "011" => y <= a or b;
      when "100" => y <= a xor b;
      when "101" => y <= std_logic_vector(shift_left(unsigned(a), shamt));
      when "110" => y <= std_logic_vector(shift_right(unsigned(a), shamt));
      when others =>
        if signed(a) < signed(b) then y <= x"0001"; else y <= x"0000"; end if;
    end case;
  end process;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity cpu16 is
  port(clk, reset : in  std_logic;
       display    : out std_logic_vector(15 downto 0);
       pc_out     : out std_logic_vector(7 downto 0);
       halted     : out std_logic);
end cpu16;

architecture rtl of cpu16 is
  type rom_t is array (0 to 255) of std_logic_vector(15 downto 0);
  constant ROM : rom_t := (
    0 => x"2200",
    1 => x"2401",
    2 => x"3610",
    3 => x"283f",
    4 => x"2a06",
    5 => x"0c6e",
    6 => x"0e62",
    7 => x"0dbc",
    8 => x"2e02",
    9 => x"0dbd",
    10 => x"ac40",
    11 => x"0250",
    12 => x"72f9",
    13 => x"b200",
    14 => x"f000",
    others => x"0000");

  type regs_t is array (0 to 7) of std_logic_vector(15 downto 0);
  signal regs : regs_t := (others => (others => '0'));
  type dmem_t is array (0 to 255) of std_logic_vector(15 downto 0);
  signal dmem : dmem_t := (others => (others => '0'));
  -- the screen: 4096 pixels at {y, x}, drawn by Wire as 64 across
  type screen_t is array (0 to 4095) of std_logic_vector(7 downto 0);
  signal pixels : screen_t := (others => (others => '0'));
  attribute wire_screen : integer;
  attribute wire_screen of pixels : signal is 64;

  signal pc, pc_next, pc_inc, pc_br : std_logic_vector(7 downto 0);
  signal instr : std_logic_vector(15 downto 0);
  signal op : std_logic_vector(3 downto 0);
  signal rd, rs, rt, f : std_logic_vector(2 downto 0);
  signal imm6, imm9, lui_v : std_logic_vector(15 downto 0);
  signal sign6 : std_logic_vector(9 downto 0);
  signal sign9 : std_logic_vector(6 downto 0);
  signal rd_v, rs_v, rt_v, b, alu_y, ld_v, wb : std_logic_vector(15 downto 0);
  signal alu_f : std_logic_vector(2 downto 0);
  signal mem_a : std_logic_vector(7 downto 0);
  signal reg_we, mem_we, px_we, out_we, taken, halt_q, is_hlt : std_logic;
  signal disp_q : std_logic_vector(15 downto 0);
begin
  instr <= ROM(to_integer(unsigned(pc)));
  op <= instr(15 downto 12);
  rd <= instr(11 downto 9);
  rs <= instr(8 downto 6);
  rt <= instr(5 downto 3);
  f  <= instr(2 downto 0);
  sign6 <= (others => instr(5));
  sign9 <= (others => instr(8));
  imm6 <= sign6 & instr(5 downto 0);
  imm9 <= sign9 & instr(8 downto 0);
  lui_v <= instr(7 downto 0) & x"00";

  rd_v <= regs(to_integer(unsigned(rd)));
  rs_v <= regs(to_integer(unsigned(rs)));
  rt_v <= regs(to_integer(unsigned(rt)));

  b <= rt_v when op = "0000" else imm6;
  alu_f <= f when op = "0000" else "000";
  u_alu: entity work.alu16 port map (a => rs_v, b => b, f => alu_f, y => alu_y);

  mem_a <= alu_y(7 downto 0);              -- ld/st: rs + imm6
  ld_v <= dmem(to_integer(unsigned(mem_a)));

  with op select wb <=
    alu_y when "0000" | "0001",
    imm9  when "0010",
    lui_v when "0011",
    ld_v  when others;

  is_hlt <= '1' when op = "1111" else '0';
  reg_we <= '1' when halt_q = '0' and (op = "0000" or op = "0001" or op = "0010" or op = "0011" or op = "0100") else '0';
  mem_we <= '1' when halt_q = '0' and op = "0101" else '0';
  px_we  <= '1' when halt_q = '0' and op = "1010" else '0';
  out_we <= '1' when halt_q = '0' and op = "1011" else '0';

  taken <= '1' when (op = "0110" and rd_v = rs_v) or (op = "0111" and rd_v /= rs_v) else '0';
  pc_inc <= std_logic_vector(unsigned(pc) + 1);
  pc_br  <= std_logic_vector(unsigned(pc) + unsigned(imm6(7 downto 0)));
  pc_next <= pc     when halt_q = '1' or is_hlt = '1' else
             pc_br  when taken = '1' else
             instr(7 downto 0) when op = "1000" else
             pc_inc;

  process (clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then
        pc <= x"00";
        halt_q <= '0';
        disp_q <= x"0000";
      else
        pc <= pc_next;
        if is_hlt = '1' then halt_q <= '1'; end if;
        if out_we = '1' then disp_q <= rd_v; end if;
      end if;
    end if;
  end process;

  process (clk)
  begin
    if rising_edge(clk) then
      if reg_we = '1' then
        regs(to_integer(unsigned(rd))) <= wb;
      end if;
    end if;
  end process;

  process (clk)
  begin
    if rising_edge(clk) then
      if mem_we = '1' then
        dmem(to_integer(unsigned(mem_a))) <= rd_v;
      end if;
    end if;
  end process;

  process (clk)
  begin
    if rising_edge(clk) then
      if px_we = '1' then
        pixels(to_integer(unsigned(rs_v(11 downto 0)))) <= rd_v(7 downto 0);
      end if;
    end if;
  end process;

  display <= disp_q;
  pc_out <= pc;
  halted <= halt_q;
end rtl;
