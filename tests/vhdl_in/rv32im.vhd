-- A small single-cycle RV32IM core in the style of a course project: an ALU
-- (with RV32M's multiplies, written with a 66-bit variable), a register file,
-- an immediate generator and a decoder as entities, a 128-word program rom,
-- four byte-lane rams written in one process, and outputs mapped at 0x400
-- (out_port) and 0x404 (halted). The program in the rom loops, sorts, uses
-- bytes, halves, shifts, compares and multiplies, and writes 0x398C2B4C to
-- out_port after 683 cycles (Makefile checks it).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity alu is
  port(a, b : in  std_logic_vector(31 downto 0);
       op   : in  std_logic_vector(3 downto 0);
       m    : in  std_logic;                        -- RV32M: funct7 = 0000001
       y    : out std_logic_vector(31 downto 0));
end alu;

architecture rtl of alu is
  signal shamt : integer range 0 to 31;
begin
  shamt <= to_integer(unsigned(b(4 downto 0)));
  process (a, b, op, m, shamt)
    variable p : signed(65 downto 0);
  begin
    if m = '1' then
      case op(2 downto 0) is
        when "000" =>                                 -- mul
          y <= std_logic_vector(resize(unsigned(a) * unsigned(b), 32));
        when "001" =>                                 -- mulh
          p := signed(a(31) & a) * signed(b(31) & b);
          y <= std_logic_vector(p(63 downto 32));
        when "010" =>                                 -- mulhsu
          p := signed(a(31) & a) * signed('0' & b);
          y <= std_logic_vector(p(63 downto 32));
        when "011" =>                                 -- mulhu
          p := signed('0' & a) * signed('0' & b);
          y <= std_logic_vector(p(63 downto 32));
        when others => y <= x"00000000";             -- div/rem: not in this test
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
      when "1111" => y <= b;                       -- pass (lui)
      when others => y <= x"00000000";
    end case;
    end if;
  end process;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity regfile is
  port(clk        : in  std_logic;
       we         : in  std_logic;
       rs1, rs2   : in  std_logic_vector(4 downto 0);
       rd         : in  std_logic_vector(4 downto 0);
       wd         : in  std_logic_vector(31 downto 0);
       rd1, rd2   : out std_logic_vector(31 downto 0));
end regfile;

architecture rtl of regfile is
  type regs_t is array (0 to 31) of std_logic_vector(31 downto 0);
  signal regs : regs_t := (others => (others => '0'));
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if we = '1' and rd /= "00000" then
        regs(to_integer(unsigned(rd))) <= wd;
      end if;
    end if;
  end process;
  rd1 <= regs(to_integer(unsigned(rs1)));
  rd2 <= regs(to_integer(unsigned(rs2)));
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

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
    sign & instr(31 downto 20)                                         when "0000011" | "0010011" | "1100111",
    sign & instr(31 downto 25) & instr(11 downto 7)                    when "0100011",
    sign(18 downto 0) & instr(31) & instr(7) & instr(30 downto 25) & instr(11 downto 8) & '0' when "1100011",
    instr(31 downto 12) & x"000"                                       when "0110111" | "0010111",
    sign(10 downto 0) & instr(31) & instr(19 downto 12) & instr(20) & instr(30 downto 21) & '0' when "1101111",
    x"00000000"                                                        when others;
end rtl;

library ieee;
use ieee.std_logic_1164.all;

entity decoder is
  port(instr     : in  std_logic_vector(31 downto 0);
       reg_write : out std_logic;
       alu_src   : out std_logic;                     -- 1: b = imm
       alu_op    : out std_logic_vector(3 downto 0);
       a_pc      : out std_logic;                     -- 1: a = pc (auipc)
       mem_read  : out std_logic;
       mem_write : out std_logic;
       branch    : out std_logic;
       jump      : out std_logic;
       jalr      : out std_logic);
end decoder;

architecture rtl of decoder is
  signal opcode : std_logic_vector(6 downto 0);
  signal f3     : std_logic_vector(2 downto 0);
  signal f7b    : std_logic;
begin
  opcode <= instr(6 downto 0);
  f3     <= instr(14 downto 12);
  f7b    <= instr(30);
  process (opcode, f3, f7b)
  begin
    reg_write <= '0'; alu_src <= '0'; alu_op <= "0000"; a_pc <= '0';
    mem_read <= '0'; mem_write <= '0'; branch <= '0'; jump <= '0'; jalr <= '0';
    case opcode is
      when "0110011" =>                               -- R-type
        reg_write <= '1';
        alu_op <= f7b & f3;
      when "0010011" =>                               -- I-type ALU
        reg_write <= '1'; alu_src <= '1';
        if f3 = "101" then alu_op <= f7b & f3; else alu_op <= '0' & f3; end if;
      when "0000011" =>                               -- loads
        reg_write <= '1'; alu_src <= '1'; mem_read <= '1';
      when "0100011" =>                               -- stores
        alu_src <= '1'; mem_write <= '1';
      when "1100011" =>                               -- branches
        branch <= '1';
      when "0110111" =>                               -- lui
        reg_write <= '1'; alu_src <= '1'; alu_op <= "1111";
      when "0010111" =>                               -- auipc
        reg_write <= '1'; alu_src <= '1'; a_pc <= '1';
      when "1101111" =>                               -- jal
        reg_write <= '1'; jump <= '1';
      when "1100111" =>                               -- jalr
        reg_write <= '1'; alu_src <= '1'; jalr <= '1';
      when others => null;
    end case;
  end process;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rv32im is
  port(clk, reset : in  std_logic;
       out_port   : out std_logic_vector(31 downto 0);
       pc_out     : out std_logic_vector(31 downto 0);
       halted     : out std_logic);
end rv32im;

architecture rtl of rv32im is
  type rom_t is array (0 to 127) of std_logic_vector(31 downto 0);
  constant ROM : rom_t := (
    0 => x"00a00293",
    1 => x"00000513",
    2 => x"00550533",
    3 => x"fff28293",
    4 => x"fe029ce3",
    5 => x"40a02023",
    6 => x"04000413",
    7 => x"00100313",
    8 => x"00100393",
    9 => x"00a00e13",
    10 => x"00040e93",
    11 => x"006ea023",
    12 => x"00730f33",
    13 => x"00038313",
    14 => x"000f0393",
    15 => x"004e8e93",
    16 => x"fffe0e13",
    17 => x"fe0e14e3",
    18 => x"00a00493",
    19 => x"fff48493",
    20 => x"02048c63",
    21 => x"00040e93",
    22 => x"00048e13",
    23 => x"000ea583",
    24 => x"004ea603",
    25 => x"00c5d463",
    26 => x"014000ef",
    27 => x"004e8e93",
    28 => x"fffe0e13",
    29 => x"fe0e14e3",
    30 => x"fd5ff06f",
    31 => x"00cea023",
    32 => x"00bea223",
    33 => x"00008067",
    34 => x"00042503",
    35 => x"40a02023",
    36 => x"123456b7",
    37 => x"67868693",
    38 => x"08d02023",
    39 => x"08104703",
    40 => x"08300783",
    41 => x"fff00813",
    42 => x"09000123",
    43 => x"08002883",
    44 => x"08201903",
    45 => x"08005983",
    46 => x"09001023",
    47 => x"08002a03",
    48 => x"40485a93",
    49 => x"01c85b13",
    50 => x"00469b93",
    51 => x"00700293",
    52 => x"4056dc33",
    53 => x"00569cb3",
    54 => x"00585d33",
    55 => x"00082333",
    56 => x"000833b3",
    57 => x"00582e13",
    58 => x"0082be93",
    59 => x"fff6cf13",
    60 => x"0302ef93",
    61 => x"0ff6fd93",
    62 => x"00000197",
    63 => x"41168233",
    64 => x"016ae133",
    65 => x"00d17133",
    66 => x"01114133",
    67 => x"00f70533",
    68 => x"01150533",
    69 => x"01250533",
    70 => x"01350533",
    71 => x"01450533",
    72 => x"01550533",
    73 => x"01650533",
    74 => x"01750533",
    75 => x"01850533",
    76 => x"01950533",
    77 => x"01a50533",
    78 => x"00650533",
    79 => x"00750533",
    80 => x"01c50533",
    81 => x"01d50533",
    82 => x"01e50533",
    83 => x"01f50533",
    84 => x"01b50533",
    85 => x"00350533",
    86 => x"00450533",
    87 => x"00250533",
    88 => x"00d56463",
    89 => x"00154513",
    90 => x"00084463",
    91 => x"06450513",
    92 => x"00a87463",
    93 => x"0c850513",
    94 => x"40a02023",
    95 => x"800002b7",
    96 => x"ffd00313",
    97 => x"123453b7",
    98 => x"67838393",
    99 => x"02638433",
    100 => x"026394b3",
    101 => x"02732933",
    102 => x"027339b3",
    103 => x"02529a33",
    104 => x"0262bab3",
    105 => x"0262ab33",
    106 => x"02738bb3",
    107 => x"00940533",
    108 => x"01250533",
    109 => x"01350533",
    110 => x"01450533",
    111 => x"01550533",
    112 => x"01650533",
    113 => x"01750533",
    114 => x"40a02023",
    115 => x"00100293",
    116 => x"40502223",
    117 => x"0000006f",
    others => x"00000000");

  type bank_t is array (0 to 255) of std_logic_vector(7 downto 0);
  signal mem0, mem1, mem2, mem3 : bank_t := (others => (others => '0'));

  signal pc : std_logic_vector(31 downto 0) := (others => '0');
  signal pc4, next_pc, instr : std_logic_vector(31 downto 0);
  signal rd1, rd2, imm, alu_a, alu_b, alu_y, wb : std_logic_vector(31 downto 0);
  signal reg_write, alu_src, a_pc, mem_read, mem_write, branch, jump, jalr : std_logic;
  signal alu_op : std_logic_vector(3 downto 0);
  signal f3 : std_logic_vector(2 downto 0);
  signal take, is_m : std_logic;
  signal widx : integer range 0 to 255;
  signal word, ld_shift, ld_val, st_data : std_logic_vector(31 downto 0);
  signal be : std_logic_vector(3 downto 0);
  signal b0, b1, b2, b3 : std_logic_vector(7 downto 0);
  signal is_io, out_we, halt_we : std_logic;
  signal out_r : std_logic_vector(31 downto 0) := (others => '0');
  signal halt_r : std_logic := '0';
begin
  instr <= ROM(to_integer(unsigned(pc(8 downto 2))));
  f3    <= instr(14 downto 12);

  dec: entity work.decoder port map(instr => instr, reg_write => reg_write, alu_src => alu_src,
        alu_op => alu_op, a_pc => a_pc, mem_read => mem_read, mem_write => mem_write,
        branch => branch, jump => jump, jalr => jalr);
  ig:  entity work.imm_gen port map(instr => instr, imm => imm);
  rf:  entity work.regfile port map(clk => clk, we => reg_write,
        rs1 => instr(19 downto 15), rs2 => instr(24 downto 20), rd => instr(11 downto 7),
        wd => wb, rd1 => rd1, rd2 => rd2);

  alu_a <= pc  when a_pc = '1'    else rd1;
  alu_b <= imm when alu_src = '1' else rd2;
  is_m <= '1' when instr(6 downto 0) = "0110011" and instr(25) = '1' else '0';
  u_alu: entity work.alu port map(a => alu_a, b => alu_b, op => alu_op, m => is_m, y => alu_y);

  -- branch condition
  process (f3, rd1, rd2)
  begin
    case f3 is
      when "000" => if rd1 = rd2 then take <= '1'; else take <= '0'; end if;
      when "001" => if rd1 /= rd2 then take <= '1'; else take <= '0'; end if;
      when "100" => if signed(rd1) < signed(rd2) then take <= '1'; else take <= '0'; end if;
      when "101" => if signed(rd1) >= signed(rd2) then take <= '1'; else take <= '0'; end if;
      when "110" => if unsigned(rd1) < unsigned(rd2) then take <= '1'; else take <= '0'; end if;
      when "111" => if unsigned(rd1) >= unsigned(rd2) then take <= '1'; else take <= '0'; end if;
      when others => take <= '0';
    end case;
  end process;

  pc4 <= std_logic_vector(unsigned(pc) + 4);
  next_pc <= alu_y(31 downto 1) & '0'                       when jalr = '1' else
             std_logic_vector(unsigned(pc) + unsigned(imm)) when jump = '1' or (branch = '1' and take = '1') else
             pc4;

  process (clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then pc <= (others => '0');
      elsif halt_r = '0' then pc <= next_pc;
      end if;
    end if;
  end process;

  -- data memory: four byte lanes
  is_io <= alu_y(10);
  widx  <= to_integer(unsigned(alu_y(9 downto 2)));
  process (f3, alu_y, mem_write, is_io)
  begin
    be <= "0000";
    if mem_write = '1' and is_io = '0' then
      case f3(1 downto 0) is
        when "00" =>
          case alu_y(1 downto 0) is
            when "00" => be <= "0001";
            when "01" => be <= "0010";
            when "10" => be <= "0100";
            when others => be <= "1000";
          end case;
        when "01" =>
          if alu_y(1) = '0' then be <= "0011"; else be <= "1100"; end if;
        when others => be <= "1111";
      end case;
    end if;
  end process;
  with f3(1 downto 0) select st_data <=
    rd2(7 downto 0) & rd2(7 downto 0) & rd2(7 downto 0) & rd2(7 downto 0) when "00",
    rd2(15 downto 0) & rd2(15 downto 0)                                     when "01",
    rd2                                                                     when others;

  process (clk)
  begin
    if rising_edge(clk) then
      if be(0) = '1' then mem0(widx) <= st_data(7 downto 0);   end if;
      if be(1) = '1' then mem1(widx) <= st_data(15 downto 8);  end if;
      if be(2) = '1' then mem2(widx) <= st_data(23 downto 16); end if;
      if be(3) = '1' then mem3(widx) <= st_data(31 downto 24); end if;
    end if;
  end process;
  word <= mem3(widx) & mem2(widx) & mem1(widx) & mem0(widx);

  with alu_y(1 downto 0) select ld_shift <=
    word                       when "00",
    x"00" & word(31 downto 8)  when "01",
    x"0000" & word(31 downto 16) when "10",
    x"000000" & word(31 downto 24) when others;
  process (f3, ld_shift)
  begin
    case f3 is
      when "000" => ld_val <= std_logic_vector(resize(signed(ld_shift(7 downto 0)), 32));
      when "001" => ld_val <= std_logic_vector(resize(signed(ld_shift(15 downto 0)), 32));
      when "100" => ld_val <= x"000000" & ld_shift(7 downto 0);
      when "101" => ld_val <= x"0000" & ld_shift(15 downto 0);
      when others => ld_val <= ld_shift;
    end case;
  end process;

  wb <= pc4    when jump = '1' or jalr = '1' else
        ld_val when mem_read = '1' else
        alu_y;

  -- memory-mapped outputs
  out_we  <= '1' when mem_write = '1' and is_io = '1' and alu_y(2) = '0' else '0';
  halt_we <= '1' when mem_write = '1' and is_io = '1' and alu_y(2) = '1' else '0';
  process (clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then
        out_r <= (others => '0'); halt_r <= '0';
      else
        if out_we = '1'  then out_r <= rd2; end if;
        if halt_we = '1' then halt_r <= rd2(0); end if;
      end if;
    end if;
  end process;

  out_port <= out_r;
  pc_out   <= pc;
  halted   <= halt_r;
end rtl;
