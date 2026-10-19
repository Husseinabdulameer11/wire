-- computer.vhd: the 8-bit datapath (full_adder) plus a control unit that
-- runs a program from the ROM, so only clk and rst are needed, and a
-- 16 x 16 screen the program draws on.
--
-- Instructions (8 bits: opcode in the high 4, operand in the low 4):
--   0 NOP          5 LDI n   A <- n          9 JC a   jump if last SUB borrowed
--   1 LDA a  A <- RAM[a]     6 JMP a         8 OUT    OUT <- A
--   2 ADD a  A <- A + RAM[a] 7 JZ a  jump if last ALU result was 0
--   3 SUB a  A <- A - RAM[a]                 F HLT    stop
--   4 STA a  RAM[a] <- A
--   A PIX c  pixel A <- colour c  (A is the place: high 4 bits y, low 4 bits x)
--   B ADI n  A <- A + n           (n is 0 to 15; the flags as for ADD)
--   C SBI n  A <- A - n           (the flags as for SUB)
-- Every instruction starts with a fetch: MAR <- PC, then IR <- ROM, PC + 1.
--
-- Colours are 3 bits, red green blue: 0 black, 1 blue, 2 green, 3 cyan,
-- 4 red, 5 magenta, 6 yellow, 7 white. A is the pixel's address {y, x}, so
-- ADI 1 is one pixel right (and on to the next line), ADI 16 one line down.
--
-- The program in the ROM paints the screen blue, then draws a red line from
-- the bottom right corner to the top left, and halts:
--    0 PIX 1    1 ADI 1    2 JZ 4     3 JMP 0     fill: A goes 0 .. 255, back to 0
--    4 SBI 1    5 PIX 4    6 SBI 15   7 SBI 2     A = 255, then 17 less each time
--    8 JZ 10    9 JMP 5   10 PIX 4   11 HLT       down to 0 (the corner), stop
--
-- Try this one instead (rows 0 to 6 of the ROM, the rest 0): stripes of red,
-- green and blue that move, because 256 pixels is not a whole number of
-- stripes, so each pass over the screen starts one colour later:
--    PIX 4  ADI 1  PIX 2  ADI 1  PIX 1  ADI 1  JMP 0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component reg8
entity reg8 is
  port (
    clk  : in  std_logic;
    rst  : in  std_logic;
    load : in  std_logic;
    d    : in  std_logic_vector(7 downto 0);
    q    : out std_logic_vector(7 downto 0)
  );
end reg8;

architecture rtl of reg8 is
  signal mux1 : std_logic_vector(7 downto 0);
  signal q_i : std_logic_vector(7 downto 0) := "00000000";
  signal mux2 : std_logic_vector(7 downto 0);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      q_i <= mux1;
    end if;
  end process;
  with rst select mux1 <=
    mux2 when '0',
    "00000000" when others;
  with load select mux2 <=
    q_i when '0',
    d when others;
  q <= q_i;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component reg4
entity reg4 is
  port (
    clk  : in  std_logic;
    rst  : in  std_logic;
    load : in  std_logic;
    d    : in  std_logic_vector(3 downto 0);
    q    : out std_logic_vector(3 downto 0)
  );
end reg4;

architecture rtl of reg4 is
  signal mux1 : std_logic_vector(3 downto 0);
  signal q_i : std_logic_vector(3 downto 0) := "0000";
  signal mux2 : std_logic_vector(3 downto 0);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      q_i <= mux1;
    end if;
  end process;
  with rst select mux1 <=
    mux2 when '0',
    "0000" when others;
  with load select mux2 <=
    q_i when '0',
    d when others;
  q <= q_i;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component rom16x8
entity rom16x8 is
  port (
    addr : in  std_logic_vector(3 downto 0);
    q    : out std_logic_vector(7 downto 0)
  );
end rom16x8;

architecture rtl of rom16x8 is
  type rom1_t is array (0 to 15) of std_logic_vector(7 downto 0);
  constant rom1_words : rom1_t := (
    0  => "10100001",   -- PIX 1    pixel A <- blue
    1  => "10110001",   -- ADI 1    next pixel
    2  => "01110100",   -- JZ 4     past the last one (A is 0 again): done
    3  => "01100000",   -- JMP 0
    4  => "11000001",   -- SBI 1    A <- 255, the bottom right corner
    5  => "10100100",   -- PIX 4    pixel A <- red
    6  => "11001111",   -- SBI 15
    7  => "11000010",   -- SBI 2    A <- A - 17: one up, one left
    8  => "01111010",   -- JZ 10    at the top left corner
    9  => "01100101",   -- JMP 5
    10 => "10100100",   -- PIX 4    the corner too
    11 => "11110000",   -- HLT
    others => (others => '0'));
begin
  q <= rom1_words(to_integer(unsigned(addr)));
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component ram16x8
entity ram16x8 is
  port (
    clk  : in  std_logic;
    we   : in  std_logic;
    addr : in  std_logic_vector(3 downto 0);
    d    : in  std_logic_vector(7 downto 0);
    q    : out std_logic_vector(7 downto 0)
  );
end ram16x8;

architecture rtl of ram16x8 is
  type ram1_t is array (0 to 15) of std_logic_vector(7 downto 0);
  signal ram1_words : ram1_t := (
    12 => "00000011",   -- 3: the number added
    14 => "00000101",   -- 5: how many times
    15 => "00000001",   -- 1: to count down with
    others => (others => '0'));
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if we = '1' then
        ram1_words(to_integer(unsigned(addr))) <= d;
      end if;
    end if;
  end process;
  q <= ram1_words(to_integer(unsigned(addr)));
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component ha
entity ha is
  port (
    a    : in  std_logic;
    b    : in  std_logic;
    sum  : out std_logic;
    cout : out std_logic
  );
end ha;

architecture rtl of ha is
begin
  sum <= a xor b;
  cout <= a and b;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component fa
entity fa is
  port (
    a    : in  std_logic;
    b    : in  std_logic;
    cin  : in  std_logic;
    sum  : out std_logic;
    cout : out std_logic
  );
end fa;

architecture rtl of fa is
  signal n1 : std_logic;
  signal n2 : std_logic;
  signal chip1_u_u_u_u_i : std_logic;
begin
  cout <= n1 or n2;
  chip1_u_u_u_u: entity work.ha port map (a => a, b => b, sum => chip1_u_u_u_u_i, cout => n2);
  chip2: entity work.ha port map (a => chip1_u_u_u_u_i, b => cin, sum => sum, cout => n1);
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component eightbitfa
entity eightbitfa is
  port (
    a    : in  std_logic_vector(7 downto 0);
    b    : in  std_logic_vector(7 downto 0);
    cin  : in  std_logic;
    cout : out std_logic;
    sum  : out std_logic_vector(7 downto 0)
  );
end eightbitfa;

architecture rtl of eightbitfa is
  signal chip8_u_u_u_u_i : std_logic;
  signal chip7_u_u_u_u_i : std_logic;
  signal chip6_u_u_u_u_i : std_logic;
  signal chip5_u_u_u_u_i : std_logic;
  signal chip4_u_u_u_u_i : std_logic;
  signal chip3_u_u_u_u_i : std_logic;
  signal chip2_u_u_u_u_i : std_logic;
  signal chip1_u_u_u_u_i : std_logic;
  signal split1_7 : std_logic;
  signal split2_0 : std_logic;
  signal n1 : std_logic;
  signal split1_6 : std_logic;
  signal split2_1 : std_logic;
  signal n2 : std_logic;
  signal split1_5 : std_logic;
  signal split2_2 : std_logic;
  signal n3 : std_logic;
  signal split1_4 : std_logic;
  signal split2_3 : std_logic;
  signal n4 : std_logic;
  signal split1_3 : std_logic;
  signal split2_4 : std_logic;
  signal n5 : std_logic;
  signal split1_2 : std_logic;
  signal split2_5 : std_logic;
  signal n6 : std_logic;
  signal split1_1 : std_logic;
  signal split2_6 : std_logic;
  signal n7 : std_logic;
  signal split1_0 : std_logic;
  signal split2_7 : std_logic;
begin
  sum <= chip8_u_u_u_u_i & chip7_u_u_u_u_i & chip6_u_u_u_u_i & chip5_u_u_u_u_i & chip4_u_u_u_u_i & chip3_u_u_u_u_i & chip2_u_u_u_u_i & chip1_u_u_u_u_i;
  split1_0 <= a(7);
  split1_1 <= a(6);
  split1_2 <= a(5);
  split1_3 <= a(4);
  split1_4 <= a(3);
  split1_5 <= a(2);
  split1_6 <= a(1);
  split1_7 <= a(0);
  split2_0 <= b(0);
  split2_1 <= b(1);
  split2_2 <= b(2);
  split2_3 <= b(3);
  split2_4 <= b(4);
  split2_5 <= b(5);
  split2_6 <= b(6);
  split2_7 <= b(7);
  chip1_u_u_u_u: entity work.fa port map (a => split1_7, b => split2_0, cin => cin, sum => chip1_u_u_u_u_i, cout => n1);
  chip2_u_u_u_u: entity work.fa port map (a => split1_6, b => split2_1, cin => n1, sum => chip2_u_u_u_u_i, cout => n2);
  chip3_u_u_u_u: entity work.fa port map (a => split1_5, b => split2_2, cin => n2, sum => chip3_u_u_u_u_i, cout => n3);
  chip4_u_u_u_u: entity work.fa port map (a => split1_4, b => split2_3, cin => n3, sum => chip4_u_u_u_u_i, cout => n4);
  chip5_u_u_u_u: entity work.fa port map (a => split1_3, b => split2_4, cin => n4, sum => chip5_u_u_u_u_i, cout => n5);
  chip6_u_u_u_u: entity work.fa port map (a => split1_2, b => split2_5, cin => n5, sum => chip6_u_u_u_u_i, cout => n6);
  chip7_u_u_u_u: entity work.fa port map (a => split1_1, b => split2_6, cin => n6, sum => chip7_u_u_u_u_i, cout => n7);
  chip8_u_u_u_u: entity work.fa port map (a => split1_0, b => split2_7, cin => n7, sum => chip8_u_u_u_u_i, cout => cout);
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component full_adder_c
entity full_adder_c is
  port (
    a   : in  std_logic_vector(7 downto 0);
    b   : in  std_logic_vector(7 downto 0);
    c   : in  std_logic;
    sum : out std_logic_vector(7 downto 0)
  );
end full_adder_c;

architecture rtl of full_adder_c is
  signal chip1_u_u_u_u_i : std_logic;
begin
  chip1_u_u_u_u: entity work.eightbitfa port map (a => a, b => b, cin => c, cout => chip1_u_u_u_u_i, sum => sum);
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component eightbitsub
entity eightbitsub is
  port (
    a    : in  std_logic_vector(7 downto 0);
    b    : in  std_logic_vector(7 downto 0);
    diff : out std_logic_vector(7 downto 0);
    bout : out std_logic
  );
end eightbitsub;

architecture rtl of eightbitsub is
  signal nb : std_logic_vector(7 downto 0);
  signal u_add_u_u_i : std_logic;
begin
  nb <= not b;
  bout <= not u_add_u_u_i;
  u_add_u_u: entity work.eightbitfa port map (a => a, b => nb, cin => '1', cout => u_add_u_u_i, sum => diff);
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component full_adder_c2
entity full_adder_c2 is
  port (
    a    : in  std_logic_vector(7 downto 0);
    b    : in  std_logic_vector(7 downto 0);
    c    : in  std_logic;
    d    : in  std_logic;
    bout : out std_logic;
    y    : out std_logic_vector(7 downto 0)
  );
end full_adder_c2;

architecture rtl of full_adder_c2 is
  signal u_adder_u_u_i : std_logic_vector(7 downto 0);
  signal u_sub_u_u_i : std_logic_vector(7 downto 0);
begin
  with d select y <=
    u_adder_u_u_i when '0',
    u_sub_u_u_i when others;
  u_adder_u_u: entity work.full_adder_c port map (a => a, b => b, c => c, sum => u_adder_u_u_i);
  u_sub_u_u: entity work.eightbitsub port map (a => a, b => b, diff => u_sub_u_u_i, bout => bout);
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- the circuit
entity full_adder is
  port (
    rst      : in  std_logic;
    sw       : in  std_logic_vector(7 downto 0);
    sw_out   : in  std_logic;
    a_out    : in  std_logic;
    alu_out  : in  std_logic;
    rom_out  : in  std_logic;
    ram_out  : in  std_logic;
    a_load   : in  std_logic;
    b_load   : in  std_logic;
    out_load : in  std_logic;
    mar_load : in  std_logic;
    ram_in   : in  std_logic;
    sub      : in  std_logic;
    clk      : in  std_logic;
    data_bus : out std_logic_vector(7 downto 0);
    out_q    : out std_logic_vector(7 downto 0);
    a_q      : out std_logic_vector(7 downto 0);   -- the A register: where PIX draws
    borrow   : out std_logic
  );
end full_adder;

architecture rtl of full_adder is
  signal mux1 : std_logic_vector(7 downto 0);
  signal data_bus_i : std_logic_vector(7 downto 0);
  signal mux2 : std_logic_vector(7 downto 0);
  signal u_rega_u_i : std_logic_vector(7 downto 0);
  signal mux3 : std_logic_vector(7 downto 0);
  signal n1 : std_logic_vector(7 downto 0);
  signal mux4 : std_logic_vector(7 downto 0);
  signal u_rom_u_i : std_logic_vector(7 downto 0);
  signal u_ram_u_i : std_logic_vector(7 downto 0);
  signal u_regb_u_i : std_logic_vector(7 downto 0);
  signal split1_0 : std_logic_vector(3 downto 0);
  signal u_mar_u_i : std_logic_vector(3 downto 0);
begin
  with sw_out select data_bus_i <=
    mux1 when '0',
    sw when others;
  with a_out select mux1 <=
    mux2 when '0',
    u_rega_u_i when others;
  with alu_out select mux2 <=
    mux3 when '0',
    n1 when others;
  with rom_out select mux3 <=
    mux4 when '0',
    u_rom_u_i when others;
  with ram_out select mux4 <=
    "00000000" when '0',
    u_ram_u_i when others;
  split1_0 <= data_bus_i(3 downto 0);
  u_rega_u: entity work.reg8 port map (clk => clk, rst => rst, load => a_load, d => data_bus_i, q => u_rega_u_i);
  u_regb_u: entity work.reg8 port map (clk => clk, rst => rst, load => b_load, d => data_bus_i, q => u_regb_u_i);
  u_out: entity work.reg8 port map (clk => clk, rst => rst, load => out_load, d => data_bus_i, q => out_q);
  u_mar_u: entity work.reg4 port map (clk => clk, rst => rst, load => mar_load, d => split1_0, q => u_mar_u_i);
  u_rom_u: entity work.rom16x8 port map (addr => u_mar_u_i, q => u_rom_u_i);
  u_ram_u: entity work.ram16x8 port map (clk => clk, we => ram_in, addr => u_mar_u_i, d => data_bus_i, q => u_ram_u_i);
  u_alu: entity work.full_adder_c2 port map (a => u_rega_u_i, b => u_regb_u_i, c => '0', d => sub, bout => borrow, y => n1);
  data_bus <= data_bus_i;
  a_q <= u_rega_u_i;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- component control: program counter, instruction register, step counter
-- and the control lines for each step of each instruction
entity control is
  port (
    clk      : in  std_logic;
    rst      : in  std_logic;
    bus_in   : in  std_logic_vector(7 downto 0);   -- the data bus
    borrow   : in  std_logic;                      -- from the ALU
    val      : out std_logic_vector(7 downto 0);   -- PC or operand, onto the bus via sw
    sw_out   : out std_logic;
    a_out    : out std_logic;
    alu_out  : out std_logic;
    rom_out  : out std_logic;
    ram_out  : out std_logic;
    a_load   : out std_logic;
    b_load   : out std_logic;
    out_load : out std_logic;
    mar_load : out std_logic;
    ram_in   : out std_logic;
    sub      : out std_logic;
    px_we    : out std_logic;                      -- PIX: write the bus to pixel A
    pc_q     : out std_logic_vector(3 downto 0);
    halted   : out std_logic
  );
end control;

architecture rtl of control is
  signal pc      : std_logic_vector(3 downto 0) := "0000";
  signal ir      : std_logic_vector(7 downto 0) := "00000000";
  signal step    : std_logic_vector(2 downto 0) := "000";
  signal zf      : std_logic := '0';
  signal cf      : std_logic := '0';
  signal hlt_q   : std_logic := '0';
  signal ir_load : std_logic;
  signal pc_inc  : std_logic;
  signal jump    : std_logic;
  signal done    : std_logic;
  signal halt    : std_logic;
  signal alu_o   : std_logic;
begin
  -- what each step does
  process (step, ir, pc, zf, cf)
  begin
    val <= "00000000";
    sw_out <= '0'; a_out <= '0'; alu_o <= '0'; rom_out <= '0'; ram_out <= '0';
    a_load <= '0'; b_load <= '0'; out_load <= '0'; mar_load <= '0'; ram_in <= '0';
    sub <= '0'; ir_load <= '0'; pc_inc <= '0'; jump <= '0'; done <= '0'; halt <= '0';
    px_we <= '0';
    if step = "000" then                -- fetch 1: MAR <- PC
      val <= "0000" & pc; sw_out <= '1'; mar_load <= '1';
    elsif step = "001" then             -- fetch 2: IR <- ROM, PC + 1
      rom_out <= '1'; ir_load <= '1'; pc_inc <= '1';
    elsif step = "010" then             -- first step of the instruction
      case ir(7 downto 4) is
        when "0001" | "0010" | "0011" | "0100" =>   -- LDA ADD SUB STA: MAR <- a
          val <= "0000" & ir(3 downto 0); sw_out <= '1'; mar_load <= '1';
        when "0101" =>                              -- LDI: A <- n
          val <= "0000" & ir(3 downto 0); sw_out <= '1'; a_load <= '1'; done <= '1';
        when "0110" =>                              -- JMP
          val <= "0000" & ir(3 downto 0); sw_out <= '1'; jump <= '1'; done <= '1';
        when "0111" =>                              -- JZ
          if zf = '1' then val <= "0000" & ir(3 downto 0); sw_out <= '1'; jump <= '1'; end if;
          done <= '1';
        when "1001" =>                              -- JC
          if cf = '1' then val <= "0000" & ir(3 downto 0); sw_out <= '1'; jump <= '1'; end if;
          done <= '1';
        when "1000" =>                              -- OUT
          a_out <= '1'; out_load <= '1'; done <= '1';
        when "1010" =>                              -- PIX: pixel A <- c
          val <= "00000" & ir(2 downto 0); sw_out <= '1'; px_we <= '1'; done <= '1';
        when "1011" | "1100" =>                     -- ADI SBI: B <- n
          val <= "0000" & ir(3 downto 0); sw_out <= '1'; b_load <= '1';
        when "1111" =>                              -- HLT
          halt <= '1'; done <= '1';
        when others =>                              -- NOP
          done <= '1';
      end case;
    elsif step = "011" then
      case ir(7 downto 4) is
        when "0001" =>                              -- LDA: A <- RAM
          ram_out <= '1'; a_load <= '1'; done <= '1';
        when "0010" | "0011" =>                     -- ADD SUB: B <- RAM
          ram_out <= '1'; b_load <= '1';
        when "0100" =>                              -- STA: RAM <- A
          a_out <= '1'; ram_in <= '1'; done <= '1';
        when "1011" =>                              -- ADI: A <- A + B
          alu_o <= '1'; a_load <= '1'; done <= '1';
        when "1100" =>                              -- SBI: A <- A - B
          alu_o <= '1'; a_load <= '1'; sub <= '1'; done <= '1';
        when others =>
          done <= '1';
      end case;
    else                                            -- ADD SUB: A <- ALU
      alu_o <= '1'; a_load <= '1'; done <= '1';
      if ir(7 downto 4) = "0011" then sub <= '1'; end if;
    end if;
  end process;

  -- the registers, on the clock
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        pc <= "0000"; ir <= "00000000"; step <= "000"; zf <= '0'; cf <= '0'; hlt_q <= '0';
      elsif hlt_q = '0' then
        if done = '1' then
          step <= "000";
        else
          step <= std_logic_vector(unsigned(step) + 1);
        end if;
        if ir_load = '1' then
          ir <= bus_in;
        end if;
        if jump = '1' then
          pc <= bus_in(3 downto 0);
        elsif pc_inc = '1' then
          pc <= std_logic_vector(unsigned(pc) + 1);
        end if;
        if alu_o = '1' then                         -- flags of the ALU result
          if bus_in = "00000000" then zf <= '1'; else zf <= '0'; end if;
          cf <= borrow;
        end if;
        if halt = '1' then
          hlt_q <= '1';
        end if;
      end if;
    end if;
  end process;

  alu_out <= alu_o;
  pc_q <= pc;
  halted <= hlt_q;
end rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- the circuit: your datapath, driven by the control unit, and the screen
entity computer is
  port (
    clk      : in  std_logic;
    rst      : in  std_logic;
    out_q    : out std_logic_vector(7 downto 0);
    data_bus : out std_logic_vector(7 downto 0);
    pc_q     : out std_logic_vector(3 downto 0);
    halted   : out std_logic
  );
end computer;

architecture rtl of computer is
  signal bus_s, val, a_q : std_logic_vector(7 downto 0);
  signal sw_out, a_out, alu_out, rom_out, ram_out : std_logic;
  signal a_load, b_load, out_load, mar_load, ram_in, sub, borrow, px_we : std_logic;
  -- the screen: 256 pixels of 3 bits (red, green, blue), at {y, x}. Written
  -- like a ram; on the board a VGA circuit would read it out. wire_screen
  -- tells Wire to draw it as a 16-pixel-wide screen (Vivado ignores it).
  type screen_t is array (0 to 255) of std_logic_vector(2 downto 0);
  signal pixels : screen_t := (others => (others => '0'));
  attribute wire_screen : integer;
  attribute wire_screen of pixels : signal is 16;
begin
  -- PIX: the colour is on the bus, the place is in A
  process (clk)
  begin
    if rising_edge(clk) then
      if px_we = '1' then
        pixels(to_integer(unsigned(a_q))) <= bus_s(2 downto 0);
      end if;
    end if;
  end process;

  u_ctl: entity work.control port map (
    clk => clk, rst => rst, bus_in => bus_s, borrow => borrow, val => val,
    sw_out => sw_out, a_out => a_out, alu_out => alu_out, rom_out => rom_out, ram_out => ram_out,
    a_load => a_load, b_load => b_load, out_load => out_load, mar_load => mar_load, ram_in => ram_in,
    sub => sub, px_we => px_we, pc_q => pc_q, halted => halted);
  u_dp: entity work.full_adder port map (
    rst => rst, sw => val, sw_out => sw_out, a_out => a_out, alu_out => alu_out, rom_out => rom_out,
    ram_out => ram_out, a_load => a_load, b_load => b_load, out_load => out_load, mar_load => mar_load,
    ram_in => ram_in, sub => sub, clk => clk, data_bus => bus_s, out_q => out_q, a_q => a_q, borrow => borrow);
  data_bus <= bus_s;
end rtl;