-- ============================================================================
-- tristate_demo.vhd
-- Examples of tristate ('Z') logic in VHDL:
--   1. tristate_buf : a single tristate buffer (output enable)
--   2. bidir_pin    : a bidirectional I/O pin (inout), like a data bus pin
--   3. shared_bus   : 4 devices driving one shared 8-bit bus, only one at a time
--   4. tb_tristate  : testbench that exercises all of the above
--
-- Works in VHDL-93 and VHDL-2008. Run with GHDL:
--   ghdl -a tristate_demo.vhd
--   ghdl -e tb_tristate
--   ghdl -r tb_tristate --wave=tb.ghw
-- ============================================================================

library ieee;
use ieee.std_logic_1164.all;

-- ----------------------------------------------------------------------------
-- 1. Simple tristate buffer
--    oe = '1' -> output follows input
--    oe = '0' -> output is high impedance ('Z'), i.e. disconnected
-- ----------------------------------------------------------------------------
entity tristate_buf is
    generic (WIDTH : positive := 8);
    port (
        d   : in  std_logic_vector(WIDTH-1 downto 0);
        oe  : in  std_logic;
        q   : out std_logic_vector(WIDTH-1 downto 0)
    );
end entity;

architecture rtl of tristate_buf is
begin
    q <= d when oe = '1' else (others => 'Z');
end architecture;

-- ----------------------------------------------------------------------------
-- 2. Bidirectional pin
--    When oe = '1' we drive the pin with dout.
--    When oe = '0' we release the pin ('Z') so something external can drive
--    it, and we read its value through din.
-- ----------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity bidir_pin is
    generic (WIDTH : positive := 8);
    port (
        pad  : inout std_logic_vector(WIDTH-1 downto 0);  -- the physical pin(s)
        dout : in    std_logic_vector(WIDTH-1 downto 0);  -- what we want to write
        oe   : in    std_logic;                           -- 1 = we drive the pin
        din  : out   std_logic_vector(WIDTH-1 downto 0)   -- what is on the pin
    );
end entity;

architecture rtl of bidir_pin is
begin
    pad <= dout when oe = '1' else (others => 'Z');
    din <= pad;   -- always readable (reads back our own value when driving)
end architecture;

-- ----------------------------------------------------------------------------
-- 3. Shared bus with 4 drivers
--    'sel' picks which device owns the bus. 'bus_en' = '0' leaves the bus
--    floating (all drivers 'Z'). std_logic is a *resolved* type, so several
--    concurrent drivers on one signal are legal; 'Z' loses to '0'/'1'.
--    If two devices drive different values at once you get 'X' (contention).
-- ----------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity shared_bus is
    port (
        dev0, dev1, dev2, dev3 : in  std_logic_vector(7 downto 0);
        sel                    : in  std_logic_vector(1 downto 0);
        bus_en                 : in  std_logic;
        data_bus               : out std_logic_vector(7 downto 0)
    );
end entity;

architecture rtl of shared_bus is
    signal oe : std_logic_vector(3 downto 0);
begin
    -- one-hot output enables from sel
    gen_oe : for i in 0 to 3 generate
        oe(i) <= '1' when bus_en = '1' and to_integer(unsigned(sel)) = i else '0';
    end generate;

    -- four separate tristate drivers on the same signal
    u0 : entity work.tristate_buf port map (d => dev0, oe => oe(0), q => data_bus);
    u1 : entity work.tristate_buf port map (d => dev1, oe => oe(1), q => data_bus);
    u2 : entity work.tristate_buf port map (d => dev2, oe => oe(2), q => data_bus);
    u3 : entity work.tristate_buf port map (d => dev3, oe => oe(3), q => data_bus);
end architecture;

-- ----------------------------------------------------------------------------
-- 4. Testbench
-- ----------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_tristate is
end entity;

architecture sim of tb_tristate is
    -- shared bus signals
    signal dev0 : std_logic_vector(7 downto 0) := x"11";
    signal dev1 : std_logic_vector(7 downto 0) := x"22";
    signal dev2 : std_logic_vector(7 downto 0) := x"33";
    signal dev3 : std_logic_vector(7 downto 0) := x"44";
    signal sel      : std_logic_vector(1 downto 0) := "00";
    signal bus_en   : std_logic := '0';
    signal data_bus : std_logic_vector(7 downto 0);

    -- bidirectional pin signals
    signal pad      : std_logic_vector(7 downto 0);
    signal dout     : std_logic_vector(7 downto 0) := x"A5";
    signal oe       : std_logic := '0';
    signal din      : std_logic_vector(7 downto 0);
    signal ext_drv  : std_logic_vector(7 downto 0) := (others => 'Z'); -- "outside world"
    signal pullup   : std_logic := '0';

    -- std_logic_vector -> string (to_string for vectors only exists in VHDL-2008)
    function slv_to_str(v : std_logic_vector) return string is
        variable s   : string(1 to v'length);
        variable idx : integer := 1;
    begin
        for i in v'range loop
            case v(i) is
                when 'U' => s(idx) := 'U';
                when 'X' => s(idx) := 'X';
                when '0' => s(idx) := '0';
                when '1' => s(idx) := '1';
                when 'Z' => s(idx) := 'Z';
                when 'W' => s(idx) := 'W';
                when 'L' => s(idx) := 'L';
                when 'H' => s(idx) := 'H';
                when others => s(idx) := '-';
            end case;
            idx := idx + 1;
        end loop;
        return s;
    end function;
begin
    dut_bus : entity work.shared_bus
        port map (dev0, dev1, dev2, dev3, sel, bus_en, data_bus);

    dut_pin : entity work.bidir_pin
        port map (pad => pad, dout => dout, oe => oe, din => din);

    -- external device sharing the pad (second driver on the same wires)
    pad <= ext_drv;

    -- optional weak pull-up: 'H' loses to '0'/'1' but wins over 'Z'
    pad <= (others => 'H') when pullup = '1' else (others => 'Z');

    stim : process
    begin
        -- ---------------- shared bus ----------------
        bus_en <= '0'; wait for 10 ns;
        assert data_bus = "ZZZZZZZZ" report "bus should float" severity error;

        bus_en <= '1';
        for i in 0 to 3 loop
            sel <= std_logic_vector(to_unsigned(i, 2));
            wait for 10 ns;
        end loop;

        sel <= "10"; wait for 10 ns;
        assert data_bus = x"33" report "dev2 should own the bus" severity error;

        -- ---------------- bidirectional pin ----------------
        -- we drive, external is released
        oe <= '1'; ext_drv <= (others => 'Z'); wait for 10 ns;
        assert din = x"A5" report "should read back own value" severity error;

        -- we release, external drives
        oe <= '0'; ext_drv <= x"3C"; wait for 10 ns;
        assert din = x"3C" report "should read external value" severity error;

        -- nobody drives: floating
        ext_drv <= (others => 'Z'); wait for 10 ns;
        assert din = "ZZZZZZZZ" report "pin should float" severity error;

        -- nobody drives but pull-up is on: weak high
        pullup <= '1'; wait for 10 ns;
        assert din = "HHHHHHHH" report "pin should be pulled high" severity error;

        -- contention: both sides drive different values -> 'X' on differing bits
        oe <= '1'; ext_drv <= x"0F"; wait for 10 ns;
        assert false report "contention: A5 vs 0F gives " & slv_to_str(din)
            severity note;

        assert false report "Simulation finished" severity note;
        wait;
    end process;
end architecture;