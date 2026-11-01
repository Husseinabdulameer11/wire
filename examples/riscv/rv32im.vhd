-- A RISC-V computer: an RV32IM processor (every instruction of RV32I, and
-- RV32M's multiply and divide), its program in a rom, 4 KB of ram, a colour
-- screen, a text screen, a hex display, switches and a keypad.
--
-- The processor (core) takes one clock cycle per instruction. Open it to see
-- its parts: the decoder, the immediate generator, the register file and the
-- ALU, with the divider inside.
--
-- Memory map (the program is in the rom at 0; .data goes to the ram):
--   0x0000 - 0x0FFF   rom, 1024 instructions
--   0x1000 - 0x1FFF   ram, 4 KB (bytes, halves, words)
--   0x2000 - 0x2FFF   colour screen, 64 x 64 pixels, a byte each (RGB 3-3-2):
--                     pixel (x, y) is at 0x2000 + 64*y + x
--   0x3000 - 0x33FF   text screen, 64 x 16 characters, a byte each:
--                     character (x, y) is at 0x3000 + 64*y + x
--   0x4000            hex display (write a word)
--   0x4004            write 1 to stop (ecall and ebreak stop too)
--   0x4008            the switches sw (read)
--   0x400C            the keypad: bits 3-0 the last key, bit 4 while held
--   0x4010            the clock cycles since reset, until it stops (read)
-- Both screens can be read back. sp starts at 0: give it the top of the
-- ram first (li sp, 0x2000).
--
-- On the page, Assembly opens an assembler: write a program (or pick one),
-- Assemble and run, and watch it on the screens. examples/riscv/programs
-- has the programs; tools/riscv.js puts hello.s in the rom below.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- divide a by b: q and r, as RISC-V's div, divu, rem, remu want them
entity divider is
  port(a, b : in  std_logic_vector(31 downto 0);
       sgn  : in  std_logic;                        -- 1: div and rem (signed)
       q, r : out std_logic_vector(31 downto 0));
end divider;

architecture rtl of divider is
begin
  long: process (a, b, sgn)
    variable ua, ub, qv, rv, t : unsigned(31 downto 0);
  begin
    -- signed: divide the sizes, then put the signs back
    ua := unsigned(a); ub := unsigned(b);
    if sgn = '1' and a(31) = '1' then ua := unsigned(not a) + 1; end if;
    if sgn = '1' and b(31) = '1' then ub := unsigned(not b) + 1; end if;
    -- long division, a bit at a time, as on paper: shift the next bit of a
    -- into the remainder; if b goes into it, take b away (the bit shifted
    -- out on top means it surely does)
    qv := (others => '0'); rv := (others => '0');
    for i in 31 downto 0 loop
      t := rv(30 downto 0) & ua(i);
      if rv(31) = '1' or t >= ub then
        rv := t - ub;
        qv(i) := '1';
      else
        rv := t;
      end if;
    end loop;
    -- by 0 the quotient is all ones and the remainder a, as RISC-V says
    if sgn = '1' and (a(31) xor b(31)) = '1' and b /= x"00000000" then qv := not qv + 1; end if;
    if sgn = '1' and a(31) = '1' then rv := not rv + 1; end if;
    q <= std_logic_vector(qv);
    r <= std_logic_vector(rv);
  end process;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity alu is
  port(a, b : in  std_logic_vector(31 downto 0);
       op   : in  std_logic_vector(3 downto 0);    -- funct7(5) & funct3
       m    : in  std_logic;                       -- RV32M: funct7 = 0000001
       y    : out std_logic_vector(31 downto 0));
end alu;

architecture rtl of alu is
  signal shamt  : integer range 0 to 31;
  signal dq, dr : std_logic_vector(31 downto 0);
  signal dsgn   : std_logic;
begin
  shamt <= to_integer(unsigned(b(4 downto 0)));
  dsgn  <= not op(0);                                -- div, rem: signed; divu, remu: not
  dv: entity work.divider port map(a => a, b => b, sgn => dsgn, q => dq, r => dr);

  ops: process (a, b, op, m, shamt, dq, dr)
    variable p : signed(65 downto 0);
  begin
    p := (others => '0');
    if m = '1' then
      case op(2 downto 0) is
        when "000" =>                                    -- mul
          y <= std_logic_vector(resize(unsigned(a) * unsigned(b), 32));
        when "001" =>                                    -- mulh
          p := signed(a(31) & a) * signed(b(31) & b);
          y <= std_logic_vector(p(63 downto 32));
        when "010" =>                                    -- mulhsu
          p := signed(a(31) & a) * signed('0' & b);
          y <= std_logic_vector(p(63 downto 32));
        when "011" =>                                    -- mulhu
          p := signed('0' & a) * signed('0' & b);
          y <= std_logic_vector(p(63 downto 32));
        when "100" | "101" => y <= dq;                   -- div, divu
        when others => y <= dr;                          -- rem, remu
      end case;
    else
      case op is
        when "0000" => y <= std_logic_vector(unsigned(a) + unsigned(b));
        when "1000" => y <= std_logic_vector(unsigned(a) - unsigned(b));
        when "0001" => y <= std_logic_vector(shift_left(unsigned(a), shamt));
        when "0010" =>
          if signed(a) < signed(b) then y <= x"00000001"; else y <= x"00000000"; end if;
        when "0011" =>
          if unsigned(a) < unsigned(b) then y <= x"00000001"; else y <= x"00000000"; end if;
        when "0100" => y <= a xor b;
        when "0101" => y <= std_logic_vector(shift_right(unsigned(a), shamt));
        when "1101" => y <= std_logic_vector(shift_right(signed(a), shamt));
        when "0110" => y <= a or b;
        when "0111" => y <= a and b;
        when "1111" => y <= b;                          -- lui: b is the immediate
        when others => y <= x"00000000";
      end case;
    end if;
  end process;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity regfile is
  port(clk      : in  std_logic;
       we       : in  std_logic;
       rs1, rs2 : in  std_logic_vector(4 downto 0);
       rd       : in  std_logic_vector(4 downto 0);
       wd       : in  std_logic_vector(31 downto 0);
       rd1, rd2 : out std_logic_vector(31 downto 0));
end regfile;

architecture rtl of regfile is
  type regs_t is array (0 to 31) of std_logic_vector(31 downto 0);
  signal regs : regs_t := (others => (others => '0'));
begin
  write: process (clk)
  begin
    if rising_edge(clk) then
      if we = '1' and rd /= "00000" then                -- x0 stays 0
        regs(to_integer(unsigned(rd))) <= wd;
      end if;
    end if;
  end process;
  rd1 <= regs(to_integer(unsigned(rs1)));
  rd2 <= regs(to_integer(unsigned(rs2)));
end rtl;

library ieee;
use ieee.std_logic_1164.all;

entity imm_gen is
  port(instr : in  std_logic_vector(31 downto 0);
       imm   : out std_logic_vector(31 downto 0));
end imm_gen;

architecture rtl of imm_gen is
  signal opcode : std_logic_vector(6 downto 0);
  signal sign   : std_logic_vector(19 downto 0);
begin
  opcode <= instr(6 downto 0);
  sign   <= (others => instr(31));
  with opcode select imm <=
    sign & instr(31 downto 20)                                         when "0000011" | "0010011" | "1100111",  -- I
    sign & instr(31 downto 25) & instr(11 downto 7)                    when "0100011",                          -- S
    sign(18 downto 0) & instr(31) & instr(7) & instr(30 downto 25) & instr(11 downto 8) & '0' when "1100011",   -- B
    instr(31 downto 12) & x"000"                                       when "0110111" | "0010111",              -- U
    sign(10 downto 0) & instr(31) & instr(19 downto 12) & instr(20) & instr(30 downto 21) & '0' when "1101111", -- J
    x"00000000"                                                        when others;
end rtl;

library ieee;
use ieee.std_logic_1164.all;

entity decoder is
  port(instr     : in  std_logic_vector(31 downto 0);
       reg_write : out std_logic;
       alu_src   : out std_logic;                    -- 1: b is the immediate
       alu_op    : out std_logic_vector(3 downto 0);
       m_ext     : out std_logic;                    -- mul/div
       a_pc      : out std_logic;                    -- 1: a is the pc (auipc)
       mem_read  : out std_logic;
       mem_write : out std_logic;
       branch    : out std_logic;
       jump      : out std_logic;
       jalr      : out std_logic;
       sys       : out std_logic);                   -- ecall, ebreak: stop
end decoder;

architecture rtl of decoder is
  signal opcode : std_logic_vector(6 downto 0);
  signal f3     : std_logic_vector(2 downto 0);
  signal f7b    : std_logic;
begin
  opcode <= instr(6 downto 0);
  f3     <= instr(14 downto 12);
  f7b    <= instr(30);
  m_ext  <= '1' when opcode = "0110011" and instr(25) = '1' else '0';
  control: process (opcode, f3, f7b)
  begin
    reg_write <= '0'; alu_src <= '0'; alu_op <= "0000"; a_pc <= '0';
    mem_read <= '0'; mem_write <= '0'; branch <= '0'; jump <= '0'; jalr <= '0'; sys <= '0';
    case opcode is
      when "0110011" =>                                  -- add, sub, mul ...
        reg_write <= '1';
        alu_op <= f7b & f3;
      when "0010011" =>                                  -- addi, srai ...
        reg_write <= '1'; alu_src <= '1';
        if f3 = "101" then alu_op <= f7b & f3; else alu_op <= '0' & f3; end if;
      when "0000011" =>                                  -- loads
        reg_write <= '1'; alu_src <= '1'; mem_read <= '1';
      when "0100011" =>                                  -- stores
        alu_src <= '1'; mem_write <= '1';
      when "1100011" =>                                  -- branches
        branch <= '1';
      when "0110111" =>                                  -- lui
        reg_write <= '1'; alu_src <= '1'; alu_op <= "1111";
      when "0010111" =>                                  -- auipc
        reg_write <= '1'; alu_src <= '1'; a_pc <= '1';
      when "1101111" =>                                  -- jal
        reg_write <= '1'; jump <= '1';
      when "1100111" =>                                  -- jalr
        reg_write <= '1'; alu_src <= '1'; jalr <= '1';
      when "1110011" =>                                  -- ecall, ebreak
        sys <= '1';
      when others => null;
    end case;
  end process;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- the processor: fetch at pc, decode, read registers, compute, then load or
-- store through the data port, and write the result back, all in one cycle
entity cpu is
  port(clk, reset : in  std_logic;
       halt       : in  std_logic;                      -- stopped: the pc stays
       pc_out     : out std_logic_vector(31 downto 0);  -- the instruction's address
       instr      : in  std_logic_vector(31 downto 0);  -- ... and the instruction
       d_addr     : out std_logic_vector(31 downto 0);  -- data address
       d_wdata    : out std_logic_vector(31 downto 0);  -- data to store, in its byte lanes
       d_be       : out std_logic_vector(3 downto 0);   -- which bytes to store
       d_rdata    : in  std_logic_vector(31 downto 0);  -- the word at d_addr
       sys        : out std_logic);                     -- ecall or ebreak
end cpu;

architecture rtl of cpu is
  signal pc : std_logic_vector(31 downto 0) := (others => '0');
  signal pc4, next_pc, imm, rd1, rd2, alu_a, alu_b, alu_y, wb : std_logic_vector(31 downto 0);
  signal reg_write, rf_we, alu_src, m_ext, a_pc, mem_read, mem_write, branch, jump, jalr : std_logic;
  signal alu_op : std_logic_vector(3 downto 0);
  signal f3 : std_logic_vector(2 downto 0);
  signal take : std_logic;
  signal ld_shift, ld_val : std_logic_vector(31 downto 0);
begin
  f3 <= instr(14 downto 12);

  dec: entity work.decoder port map(instr => instr, reg_write => reg_write, alu_src => alu_src, alu_op => alu_op,
        m_ext => m_ext, a_pc => a_pc, mem_read => mem_read, mem_write => mem_write, branch => branch,
        jump => jump, jalr => jalr, sys => sys);
  ig:  entity work.imm_gen port map(instr => instr, imm => imm);
  rf_we <= reg_write and not halt;                    -- stopped: nothing changes
  rf:  entity work.regfile port map(clk => clk, we => rf_we, rs1 => instr(19 downto 15), rs2 => instr(24 downto 20),
        rd => instr(11 downto 7), wd => wb, rd1 => rd1, rd2 => rd2);

  alu_a <= pc  when a_pc = '1'    else rd1;
  alu_b <= imm when alu_src = '1' else rd2;
  ex: entity work.alu port map(a => alu_a, b => alu_b, op => alu_op, m => m_ext, y => alu_y);

  -- the branch condition
  branch_cond: process (f3, rd1, rd2)
  begin
    case f3 is
      when "000" => if rd1 = rd2 then take <= '1'; else take <= '0'; end if;                      -- beq
      when "001" => if rd1 /= rd2 then take <= '1'; else take <= '0'; end if;                     -- bne
      when "100" => if signed(rd1) < signed(rd2) then take <= '1'; else take <= '0'; end if;      -- blt
      when "101" => if signed(rd1) >= signed(rd2) then take <= '1'; else take <= '0'; end if;     -- bge
      when "110" => if unsigned(rd1) < unsigned(rd2) then take <= '1'; else take <= '0'; end if;  -- bltu
      when "111" => if unsigned(rd1) >= unsigned(rd2) then take <= '1'; else take <= '0'; end if; -- bgeu
      when others => take <= '0';
    end case;
  end process;

  pc4 <= std_logic_vector(unsigned(pc) + 4);
  next_pc <= alu_y(31 downto 1) & '0'                       when jalr = '1' else
             std_logic_vector(unsigned(pc) + unsigned(imm)) when jump = '1' or (branch = '1' and take = '1') else
             pc4;
  pc_reg: process (clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then pc <= (others => '0');
      elsif halt = '0' then pc <= next_pc;
      end if;
    end if;
  end process;
  pc_out <= pc;

  -- stores: the byte (or half) copied into every lane, and which lanes to write
  d_addr <= alu_y;
  with f3(1 downto 0) select d_wdata <=
    rd2(7 downto 0) & rd2(7 downto 0) & rd2(7 downto 0) & rd2(7 downto 0) when "00",
    rd2(15 downto 0) & rd2(15 downto 0)                                     when "01",
    rd2                                                                     when others;
  byte_lanes: process (f3, alu_y, mem_write, halt)
  begin
    d_be <= "0000";
    if mem_write = '1' and halt = '0' then
      case f3(1 downto 0) is
        when "00" =>
          case alu_y(1 downto 0) is
            when "00" => d_be <= "0001";
            when "01" => d_be <= "0010";
            when "10" => d_be <= "0100";
            when others => d_be <= "1000";
          end case;
        when "01" =>
          if alu_y(1) = '0' then d_be <= "0011"; else d_be <= "1100"; end if;
        when others => d_be <= "1111";
      end case;
    end if;
  end process;

  -- loads: the byte or half from its lane, widened with its sign or 0s
  with alu_y(1 downto 0) select ld_shift <=
    d_rdata                          when "00",
    x"00" & d_rdata(31 downto 8)     when "01",
    x"0000" & d_rdata(31 downto 16)  when "10",
    x"000000" & d_rdata(31 downto 24) when others;
  load_ext: process (f3, ld_shift)
  begin
    case f3 is
      when "000" => ld_val <= std_logic_vector(resize(signed(ld_shift(7 downto 0)), 32));    -- lb
      when "001" => ld_val <= std_logic_vector(resize(signed(ld_shift(15 downto 0)), 32));   -- lh
      when "100" => ld_val <= x"000000" & ld_shift(7 downto 0);                             -- lbu
      when "101" => ld_val <= x"0000" & ld_shift(15 downto 0);                              -- lhu
      when others => ld_val <= ld_shift;                                                    -- lw
    end case;
  end process;

  wb <= pc4    when jump = '1' or jalr = '1' else
        ld_val when mem_read = '1' else
        alu_y;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- the computer: the processor, and what its addresses reach
entity rv32im is
  port(clk, reset : in  std_logic;
       sw         : in  std_logic_vector(7 downto 0);
       key        : in  std_logic_vector(4 downto 0);   -- the keypad
       display    : out std_logic_vector(31 downto 0);
       halted     : out std_logic;
       pc         : out std_logic_vector(31 downto 0));
end rv32im;

architecture rtl of rv32im is
  -- the program: 1024 instructions (Assembly on the page puts yours here)
  type rom_t is array (0 to 1023) of std_logic_vector(31 downto 0);
  constant program : rom_t := (
    -- program: begin (tools/riscv.js puts programs/hello.s here)
    0 => x"00002137",
    1 => x"00001537",
    2 => x"00050513",
    3 => x"000035B7",
    4 => x"08C000EF",
    5 => x"00001537",
    6 => x"00F50513",
    7 => x"000035B7",
    8 => x"08058593",
    9 => x"078000EF",
    10 => x"00001537",
    11 => x"04150513",
    12 => x"000035B7",
    13 => x"0C058593",
    14 => x"064000EF",
    15 => x"000023B7",
    16 => x"04000F13",
    17 => x"00000293",
    18 => x"00000313",
    19 => x"00335E13",
    20 => x"005E1E13",
    21 => x"0032DE93",
    22 => x"002E9E93",
    23 => x"01DE6E33",
    24 => x"0062CEB3",
    25 => x"004EDE93",
    26 => x"003EFE93",
    27 => x"01DE6E33",
    28 => x"01C38023",
    29 => x"00138393",
    30 => x"00130313",
    31 => x"FDE348E3",
    32 => x"00128293",
    33 => x"FDE2C2E3",
    34 => x"000042B7",
    35 => x"0082A303",
    36 => x"02630333",
    37 => x"0062A023",
    38 => x"FF5FF06F",
    39 => x"00054283",
    40 => x"00028A63",
    41 => x"00558023",
    42 => x"00150513",
    43 => x"00158593",
    44 => x"FEDFF06F",
    45 => x"00008067",
    -- program: end
    others => x"00000000");

  -- 4 KB of ram, a word per address, written a byte (lane) at a time
  type ram_t is array (0 to 1023) of std_logic_vector(31 downto 0);
  signal ram : ram_t := (
    -- data: begin (its .data)
    0 => x"6C6C6548",
    1 => x"52202C6F",
    2 => x"2D435349",
    3 => x"41002156",
    4 => x"5652206E",
    5 => x"4D493233",
    6 => x"6F727020",
    7 => x"73736563",
    8 => x"202C726F",
    9 => x"77617264",
    10 => x"6E61206E",
    11 => x"69732064",
    12 => x"616C756D",
    13 => x"20646574",
    14 => x"57207962",
    15 => x"2E657269",
    16 => x"696C4600",
    17 => x"68742070",
    18 => x"77732065",
    19 => x"68637469",
    20 => x"28207365",
    21 => x"3A297773",
    22 => x"65687420",
    23 => x"73696420",
    24 => x"79616C70",
    25 => x"6F687320",
    26 => x"73207377",
    27 => x"202A2077",
    28 => x"002E7773",
    -- data: end
    others => (others => '0'));

  -- the screens: a byte per pixel, a byte per character
  type pixels_t is array (0 to 4095) of std_logic_vector(7 downto 0);
  signal pixels : pixels_t := (others => (others => '0'));
  type chars_t is array (0 to 1023) of std_logic_vector(7 downto 0);
  signal chars : chars_t := (others => (others => '0'));
  attribute wire_screen : integer;
  attribute wire_screen of pixels : signal is 64;
  attribute wire_screen of chars : signal is 64;
  attribute wire_text : integer;
  attribute wire_text of chars : signal is 1;
  -- how it is programmed: Wire's assembler (Assembly) puts a program in the
  -- rom (instr) and its .data in the ram's byte lanes, from rv32im.isa
  attribute wire_cpu : string;
  attribute wire_cpu of rtl : architecture is "isa=rv32im.isa; text=instr; data=ram; data_base=0x1000; pc=pc; halt=halted; regs=regs_%d";

  signal pc_i, instr, d_addr, d_wdata, d_rdata, ram_q : std_logic_vector(31 downto 0);
  signal d_be : std_logic_vector(3 downto 0);
  signal sys, halt : std_logic;
  signal area : std_logic_vector(3 downto 0);
  signal word : integer range 0 to 1023;
  signal px_at : integer range 0 to 4095;
  signal ch_at : integer range 0 to 1023;
  signal px_q, ch_q, st_byte : std_logic_vector(7 downto 0);
  signal io_we : std_logic;
  signal disp_r : std_logic_vector(31 downto 0) := (others => '0');
  signal halt_r : std_logic := '0';
  signal cycles : unsigned(31 downto 0) := (others => '0');
begin
  core: entity work.cpu port map(clk => clk, reset => reset, halt => halt, pc_out => pc_i, instr => instr,
        d_addr => d_addr, d_wdata => d_wdata, d_be => d_be, d_rdata => d_rdata, sys => sys);

  instr <= program(to_integer(unsigned(pc_i(11 downto 2))));

  -- which part of the memory map an address is in: its bits 15-12
  area  <= d_addr(15 downto 12);
  word  <= to_integer(unsigned(d_addr(11 downto 2)));
  px_at <= to_integer(unsigned(d_addr(11 downto 0)));
  ch_at <= to_integer(unsigned(d_addr(9 downto 0)));

  ram_write: process (clk)   -- the ram, a lane per byte
  begin
    if rising_edge(clk) then
      if area = "0001" then
        if d_be(0) = '1' then ram(word)(7 downto 0)   <= d_wdata(7 downto 0);   end if;
        if d_be(1) = '1' then ram(word)(15 downto 8)  <= d_wdata(15 downto 8);  end if;
        if d_be(2) = '1' then ram(word)(23 downto 16) <= d_wdata(23 downto 16); end if;
        if d_be(3) = '1' then ram(word)(31 downto 24) <= d_wdata(31 downto 24); end if;
      end if;
    end if;
  end process;
  ram_q <= ram(word);

  pixel_write: process (clk)   -- the colour screen: a byte per pixel
  begin
    if rising_edge(clk) then
      if area = "0010" and d_be /= "0000" then pixels(px_at) <= st_byte; end if;
    end if;
  end process;
  px_q <= pixels(px_at);

  char_write: process (clk)   -- the text screen: a byte per character
  begin
    if rising_edge(clk) then
      if area = "0011" and d_be /= "0000" then chars(ch_at) <= st_byte; end if;
    end if;
  end process;
  ch_q <= chars(ch_at);

  -- the byte being stored, from its lane (for the screens)
  with d_addr(1 downto 0) select st_byte <=
    d_wdata(7 downto 0)   when "00",
    d_wdata(15 downto 8)  when "01",
    d_wdata(23 downto 16) when "10",
    d_wdata(31 downto 24) when others;

  -- the input and output registers at 0x4000
  io_we <= '1' when area = "0100" and d_be /= "0000" else '0';
  io: process (clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then
        disp_r <= (others => '0'); halt_r <= '0'; cycles <= (others => '0');
      else
        if halt_r = '0' then cycles <= cycles + 1; end if;   -- stopped: it stops counting
        if io_we = '1' and d_addr(4 downto 2) = "000" then disp_r <= d_wdata; end if;
        if (io_we = '1' and d_addr(4 downto 2) = "001" and d_wdata(0) = '1') or sys = '1' then halt_r <= '1'; end if;
      end if;
    end if;
  end process;
  halt <= halt_r;

  -- what a load reads: a screen's byte is in every lane, so any address works
  read: process (area, d_addr, ram_q, px_q, ch_q, disp_r, sw, key, cycles)
  begin
    case area is
      when "0001" => d_rdata <= ram_q;
      when "0010" => d_rdata <= px_q & px_q & px_q & px_q;
      when "0011" => d_rdata <= ch_q & ch_q & ch_q & ch_q;
      when "0100" =>
        case d_addr(4 downto 2) is
          when "000" => d_rdata <= disp_r;
          when "010" => d_rdata <= x"000000" & sw;
          when "011" => d_rdata <= "000000000000000000000000000" & key;
          when "100" => d_rdata <= std_logic_vector(cycles);
          when others => d_rdata <= x"00000000";
        end case;
      when others => d_rdata <= x"00000000";
    end case;
  end process;

  display <= disp_r;
  halted  <= halt_r;
  pc      <= pc_i;
end rtl;
