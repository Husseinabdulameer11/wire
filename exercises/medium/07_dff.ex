title: Memory: the D flip-flop
cycles: 200
random: rst=0.05
---- question
Everything so far has had no memory: the output follows the inputs at once. A **flip-flop** remembers one bit. On each **rising edge** of the clock `clk` (the moment it goes from 0 to 1), it copies `d` to `q` and keeps it until the next edge.

This one has two more inputs, as most registers do:

- `rst`: when it is 1 at a clock edge, `q` becomes 0
- `en` (enable): `q` takes `d` only when `en` is 1; otherwise it keeps its value

Things that happen on a clock edge go in a **process**:

```
process (clk)
begin
    if rising_edge(clk) then
        ...
    end if;
end process;
```

Inside, an `if` / `elsif` decides what happens. The test holds `rst` at 1 for two cycles first, then tries random inputs.
---- hint
```
process (clk)
begin
    if rising_edge(clk) then
        if rst = '1' then
            q <= '0';
        elsif en = '1' then
            q <= d;
        end if;
    end if;
end process;
```
---- starter
library ieee;
use ieee.std_logic_1164.all;

entity dff_en is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    d   : in  std_logic;
    q   : out std_logic
  );
end dff_en;

architecture rtl of dff_en is
begin
    -- your code here
end rtl;
---- solution
library ieee;
use ieee.std_logic_1164.all;

entity dff_en is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    d   : in  std_logic;
    q   : out std_logic
  );
end dff_en;

architecture rtl of dff_en is
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                q <= '0';
            elsif en = '1' then
                q <= d;
            end if;
        end if;
    end process;
end rtl;
---- wrong
library ieee;
use ieee.std_logic_1164.all;

entity dff_en is
  port(
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    d   : in  std_logic;
    q   : out std_logic
  );
end dff_en;

architecture rtl of dff_en is
begin
    process (clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                q <= '0';
            else
                q <= d;
            end if;
        end if;
    end process;
end rtl;
