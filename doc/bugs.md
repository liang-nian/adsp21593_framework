# Verified defects / 已核实缺陷

**[English](#english)** | **[中文](#中文)**

Review scope: `sharc1/` and `sharc2/` application sources, generated pinmux / `adi_initialize`, and the driver notes under `doc/`. A defect is listed only when a file and function (or a datasheet field that the programmed byte violates) show it. Items that were inspected and left unchanged are in the last section.

---

<a id="english"></a>

## English

Fifteen firmware defects were fixed. One note mis-named a SPORT argument; the calls themselves were already right and were not edited. The ADI SHARC toolchain is not on this machine, so the tree was not cross-compiled. Host `gcc` only checked the portable `ShtLog` truncation path.

### Fixed

| # | Evidence | What was wrong | Fix |
|---|----------|----------------|-----|
| 1 | `timer.c` `Tmr0Init` | `Tmr0Cfg` filled `tmr0_cfg` and `adi_tmr_Enable` ran, but `adi_tmr_Config` was never called. The 1 ms period never reached the timer, so `Uart0Poll` did not run from TMR0. | Call `adi_tmr_Config(tmr0_dev, &tmr0_cfg)` before enable. |
| 2 | `audio_flow.c` `AudioFlowInit` | TMR0 was started before UART0 RX DMA. Once the timer is actually configured, the ISR would poll DMA that is not armed yet. | Order is now SPU, UART, TMR, switch, SRU, DAC, ADC, A2B, SPORT. |
| 3 | `uart.c` `Uart0Poll` | `UART0_RXDMA_XCNT_CUR` is not a symbol in this tree. UART0 RX on ADSP-21593 is DMA channel 21. | Read `*pREG_DMA21_XCNT_CUR` from `<sys/platform.h>`. |
| 4 | `uart.c` `Uart0Poll` | `XCNT_CUR` is transfers still remaining. A read of 0 produced write index `DMA_LEN` (512), one past the buffer. | Clamp, then map `DMA_LEN` to index 0. |
| 5 | `uart.c` `Uart0Poll` | The wrap case required `cur >= old && cur < old`, which is impossible, so a write pointer that wrapped past the reader never incremented `overrun_cnt`. Landing on the reader (full buffer, which aliases empty) was also ignored. | Count overrun when the writer passes or lands on the reader, including the wrap. A full lap that lands back on the old index is still invisible; that limit is documented in `doc/adsp21593_framework.md`. |
| 6 | `uart.c` `Uart0Wr` | `len == 0`, a NULL pointer, or a length that is an exact multiple of 512 queued a DMA with `XCount == 0`. That completion interrupt never arrives, so `is_tx_over` stayed 0 and later writes timed out. | Reject NULL / zero. After the chunk loop, return when nothing remains. Each wait reloads its own timeout. |
| 7 | `log.c` `ShtLog` | No `<stdarg.h>` / `<stdio.h>`, so `va_list` and `vsnprintf` were undeclared. `if (len > 128)` is off by one: `vsnprintf` returns `>= sizeof(msg)` when it truncates, and that count walks off the 128-byte buffer. | Include `<stdarg.h>`, `<stddef.h>`, `<stdio.h>`. Send at most 127 bytes. Negative or empty results send nothing. |
| 8 | `ad2428.c`, `adau1962.c`, `adau1979.c`, `sport.c` | They call variadic `ShtLog` but included `uart.h`, which does not prototype it. | Include `log/log.h`. |
| 9 | `adau19790_reg.c` `SAI_CTRL0` | `0x23` is SAI=`100` (TDM16) and FS=`011` (64–96 kHz). This path is 48 kHz. ADAU1979 FS=`010` is 32–48 kHz, so the byte is `0x22`. | Program `0x22`. |
| 10 | `twi.c` `Twi2DevRegLngWr` | Two `adi_twi_Write` calls (address with a repeated start, then the payload) start a second addressed write. The slave treats `payload[0]` as a register address. The current 829-entry A2B table is all 1-byte writes and does not hit this path; the function was still wrong. | One write: MSB-first address (1, 2, or 4 bytes) plus payload, max 128 payload bytes. Longer or illegal widths return without touching the bus. |
| 11 | `ad2428.c` `Ad2428Cfg` DELAY | A 2-byte delay used only `val_ptr[1]`. SigmaStudio’s 16-bit delay is big-endian. This table’s 15 delays are 1 byte, so behavior of the current script is unchanged. | `val_len == 2` uses `(b0 << 8) \| b1`. NULL or empty delay is 0. |
| 12 | `ad2428.c` `Ad24280Init` | Discovery ran with no check that the DAC PLL had locked or that TWI2 was free. USBi is an I2C master and does not generate BCLK/FS. | Return unless `Adau1962ClkReady()` and `Twi2IsIdle()`. `Twi2Open` refuses a second open; `Twi2Stop` is a no-op when already idle. USBi cannot be detected in software; it has to be unplugged. |
| 13 | `switch.c` `Sw0Init` | After releasing `~ADAU_RESET` the only wait was 65535 `nop`s, well under 1 ms. ADAU1962A Rev. A requires 300 ms after PU/RST before the part is guaranteed stable. | After `Twi2Stop`, spin `__builtin_emuclk` for 300e6 cycles. At CCLK ≤ 1 GHz that is at least 300 ms. |
| 14 | `tune.c` `TuneTx`, `tune.h` | `TuneTx` sent a 4-byte length and took the CRC’s address. `TuneRx` accepts a 1-byte length only for command `0x01`, and no length for `0x02` / `0x03` / `0x04`. `tune.c` called `memset` without `<string.h>`. `tune.h` had no prototypes. | `0x01` sends one length byte (reject `len > 255`). Other commands send header + cmd + CRC only. CRC bytes are explicit little-endian. Prototypes added. `TuneRx` is still not called from `MonTmp` (unfinished; see `todo.md`). |
| 15 | `switch.c` `SwCfg` | Unused `uint8_t res`. | Removed. |

Driver notes that used to live next to this file were merged into `doc/adsp21593_framework.md`. An older sport note had called the 4th `adi_sport_ConfigClock` argument InternalClock. In CCES 3.0.3 `adi_sport_2156x.h` the order is `(h, nClockRatio, bUseIntlClock, bFallingEdge, bGatedClk)`. Every half-SPORT already passes `bUseIntlClock = false`. The SP1B `true` selects the falling edge and was not changed.

### Inspected and left as-is

- `adi_core_enable(ADI_CORE_SHARC1)` in `sharc1.c` is the CCES template: Core 1 enables Core 2. `ADI_CORE_SHARC1` is the second SHARC.
- SP1B `bFallingEdge = true` against SP1A `false` is the intentional setup/hold stagger. Do not retune it from this review.
- `AD24281` is an empty stub. The official eval board has one A2B master. `Ad24281Init` stays commented out.
- SPU IDs 63–68 (SPORT), 29 (UART0), 79/80 (UART0 RX/TX DMA) match the EZ-KIT examples.
- ADAU1962 `DAC_CTRL0 = 0x20` (TDM16, 48 kHz), `DAC_CTRL1 = 0x43`, `DAC_CTRL2 = 0x00`, PLL lock on `PLL_CLK_CTRL1` bit 2, final `PLL_CLK_CTRL0 = 0x05` (512×fs, matching 24.576 MHz `AUDIO_CLK`).
- ADAU1979 PLL lock is bit 7; `PLL = 0x03` is the 512×fs / 32–48 kHz MCS. `CMAP12 = 0x01` and `CMAP34 = 0x23` swap adjacent channels versus reset `0x10` / `0x32`. Not proven wrong.
- Switch programs the GPIO latch before `IODIR`, then releases active-low reset. That order avoids a glitch when the pins become outputs.
- A2B `i == 1` polls `MSTR_RUNNING` (INTTYPE 255). A DELAY whose previous register is `0x13` polls `DSCDONE` (INTTYPE 24). Both match this 829-entry table: the first DELAY is index 1, and every `DISCVRY` is followed by a DELAY.
- `val_width > 1` returns immediately. All 829 entries have width 1 and length 1, so this is a fail-fast, not a live bug.
- `Uart0Rd` increments `underrun_cnt` on an empty buffer. `MonTmp` loops until empty, so the counter includes idle polls. `doc/adsp21593_framework.md` describes that.
- CPU load `cnt / 1000.f` is a relative unit, as `doc/adsp21593_framework.md` says.
- `ack[]` / `nack[]` in `tune.c` are unused. CRC16 of command `0x05` is not `0x436F`. The intended payload is unknown, so the constants were not rewritten.
- No SPI driver. Pinmux only claims TWI2 and UART0. DAI audio is the SRU, not `PORT` mux. No `adi_pwr_Init` in this tree; clocks are reset defaults plus the codec PLL.
- UART divisor 136 at the 125 MHz SCLK assumed by `timer.c` is 125e6/136 ≈ 919118 baud, about 0.27% below 921600. That is the nearest integer divisor, not a defect.

### Build attempt

Recorded on this VM. There is no Makefile, CMakeLists, or Python build script in the repo.

```text
$ make -C /workspace
make: *** No targets specified and no makefile found.  Stop.

$ cmake -S /workspace -B /tmp/adsp-build
CMake Error: The source directory "/workspace" does not appear to contain CMakeLists.txt.

$ command -v cc21k cces
cc21k: not found
cces: not found
```

`/usr/bin/make`, `cmake`, `python3`, and `gcc` exist. `/opt/analog` does not. CrossCore Embedded Studio is not installed, so `cc21k` never ran. A throwaway host file that copies the `ShtLog` truncation test compiled with `gcc -Wall -Wextra` and printed `shtlog ok`. It is not part of the tree.

To build the firmware: CrossCore Embedded Studio with the SHARC toolchain, processor ADSP-21593, silicon revision 0.0, CCES projects `sharc1` (CORE1) and `sharc2`. Custom LDF `system/startup_ldf/app.ldf`. The loader kernel in `sharc1/.cproject` is the Windows path `D:\cces\SHARC\ldr\_prom.dxe` and has to be retargeted on another machine. Drivers required from the ADI pack: uart, twi, sport, tmr, spu, sru, sec, int.

---

<a id="中文"></a>

## 中文

固件里核实并改掉 15 处。旧的 sport 笔记把 `adi_sport_ConfigClock` 的第 4 参写成了 InternalClock，调用本身是对的，没有改沿。本机没有 ADI SHARC 工具链，没有做交叉编译。主机 `gcc` 只复现了 `ShtLog` 截断。

### 已修复

| # | 证据 | 问题 | 修复 |
|---|------|------|------|
| 1 | `timer.c` `Tmr0Init` | `Tmr0Cfg` 只填了 `tmr0_cfg`，接着 `adi_tmr_Enable`，没有 `adi_tmr_Config`。1 ms 周期进不了定时器，TMR0 不会调用 `Uart0Poll`。 | Enable 之前调用 `adi_tmr_Config(tmr0_dev, &tmr0_cfg)`。 |
| 2 | `audio_flow.c` `AudioFlowInit` | TMR0 早于 UART0 RX DMA。定时器一旦真的按 1 ms 跑，ISR 会去读还没武装的 DMA。 | 顺序改为 SPU → UART → TMR → switch → SRU → DAC → ADC → A2B → SPORT。 |
| 3 | `uart.c` `Uart0Poll` | `UART0_RXDMA_XCNT_CUR` 在本工程不是符号。ADSP-21593 上 UART0 RX 是 DMA21。 | 读 `*pREG_DMA21_XCNT_CUR`。 |
| 4 | `uart.c` `Uart0Poll` | `XCNT_CUR` 是剩余传输个数。读到 0 时写指针变成 `DMA_LEN`（512），越界。 | 钳位后把 `DMA_LEN` 映射为下标 0。 |
| 5 | `uart.c` `Uart0Poll` | 回绕分支写成 `cur >= old && cur < old`，恒假，写指针绕过读指针时 `overrun_cnt` 不增加。写指针落在读指针上（满缓冲与空缓冲是同一个条件）也没计数。 | 越过或落在读指针上，包括回绕，都计 overrun。整圈后停在旧下标仍然看不出来，`doc/adsp21593_framework.md` 写了这个限制。 |
| 6 | `uart.c` `Uart0Wr` | `len == 0`、空指针，或长度刚好是 512 的整数倍时，会提交 `XCount == 0` 的 DMA。完成中断不来，`is_tx_over` 停在 0，后面的写全部超时。 | 拒绝空指针和 0 长度。分块发完就返回。每次等待各自重装超时。 |
| 7 | `log.c` `ShtLog` | 没有 `<stdarg.h>` / `<stdio.h>`，`va_list` 和 `vsnprintf` 未声明。`if (len > 128)` 差 1：截断时返回值 `>= sizeof(msg)`，按这个长度发送会读出 128 字节缓冲。 | 补上头文件。最多发送 127 字节。返回值 ≤ 0 不发送。 |
| 8 | `ad2428.c`、`adau1962.c`、`adau1979.c`、`sport.c` | 调用可变参数 `ShtLog`，包含的却是没有原型的 `uart.h`。 | 改为 `log/log.h`。 |
| 9 | `adau19790_reg.c` `SAI_CTRL0` | `0x23` 的 SAI=`100`（TDM16），FS=`011`（64–96 kHz）。本链路是 48 kHz。ADAU1979 的 FS=`010` 才是 32–48 kHz，对应 `0x22`。 | 改为 `0x22`。 |
| 10 | `twi.c` `Twi2DevRegLngWr` | 两次 `adi_twi_Write`（地址带重复起始，再发数据）会开始第二次寻址写，从机把 `payload[0]` 当成寄存器地址。当前 829 条 A2B 表全是单字节写，走不到这条路径，函数本身是错的。 | 一次写入：地址按大端放 1/2/4 字节，再拼数据，数据最长 128 字节。更长或非法宽度直接返回。 |
| 11 | `ad2428.c` `Ad2428Cfg` 的 DELAY | 两字节延时只用了 `val_ptr[1]`。SigmaStudio 的 16 位延时是大端。本表 15 条延时都是 1 字节，当前脚本行为不变。 | `val_len == 2` 时用 `(b0 << 8) \| b1`。空指针或空长度当作 0。 |
| 12 | `ad2428.c` `Ad24280Init` | 发现流程不检查 DAC PLL 是否锁定，也不检查 TWI2 是否空闲。USBi 是 I2C 主机，不产生 BCLK/FS。 | `Adau1962ClkReady()` 与 `Twi2IsIdle()` 任一失败就返回。`Twi2Open` 在忙时拒绝第二次打开；`Twi2Stop` 在已空闲时什么都不做。软件看不出 USBi 还插着，必须先拔掉。 |
| 13 | `switch.c` `Sw0Init` | 释放 `~ADAU_RESET` 之后只空转 65535 次 `nop`，不到 1 ms。ADAU1962A Rev. A 要求 PU/RST 拉高后 300 ms 才保证稳定。 | `Twi2Stop` 之后用 `__builtin_emuclk` 空转 3×10^8 个周期。CCLK ≤ 1 GHz 时不少于 300 ms。 |
| 14 | `tune.c` `TuneTx`、`tune.h` | `TuneTx` 发送 4 字节长度，并把 CRC 的地址交给 `Uart0Wr`。`TuneRx` 只给命令 `0x01` 收 1 字节长度，`0x02` / `0x03` / `0x04` 没有长度。`memset` 没有 `<string.h>`。`tune.h` 没有原型。 | `0x01` 发 1 字节长度（`len > 255` 拒绝）。其它命令只发帧头 + cmd + CRC。CRC 按小端拆成两个字节。补上原型。`TuneRx` 仍未接到 `MonTmp`（`todo.md` 里的未完成项）。 |
| 15 | `switch.c` `SwCfg` | 未使用的 `uint8_t res`。 | 删除。 |

原先分散的驱动笔记已并入 `doc/adsp21593_framework.md`。旧笔记把第 4 参标成 InternalClock。CCES 3.0.3 的 `adi_sport_2156x.h` 顺序是 `(h, nClockRatio, bUseIntlClock, bFallingEdge, bGatedClk)`。六路半 SPORT 的 `bUseIntlClock` 本来就是 `false`。SP1B 的 `true` 是下降沿，没有改。

### 看过但没有改

- `sharc1.c` 里的 `adi_core_enable(ADI_CORE_SHARC1)` 是 CCES 模板：Core 1 打开 Core 2。`ADI_CORE_SHARC1` 是第二颗 SHARC。
- SP1B `bFallingEdge = true`、SP1A 为 `false`，是有意的建立/保持时间错开。这次审查不去改沿。
- `AD24281` 是空桩。官方评估板只有一路 A2B 主节点。`Ad24281Init` 保持注释。
- SPU 编号 63–68（SPORT）、29（UART0）、79/80（UART0 RX/TX DMA）与 EZ-KIT 例程一致。
- ADAU1962：`DAC_CTRL0 = 0x20`（TDM16、48 kHz），`DAC_CTRL1 = 0x43`，`DAC_CTRL2 = 0x00`，锁相在 `PLL_CLK_CTRL1` bit 2，最终 `PLL_CLK_CTRL0 = 0x05`（512×fs，对应 24.576 MHz `AUDIO_CLK`）。
- ADAU1979 锁相是 bit 7；`PLL = 0x03` 是 512×fs / 32–48 kHz 的 MCS。`CMAP12 = 0x01`、`CMAP34 = 0x23` 相对复位值 `0x10` / `0x32` 交换了相邻通道，没有证据说明这是错的。
- switch 先写 GPIO 锁存再写 `IODIR`，然后释放低有效复位。方向从输入变成输出时这样不会毛刺。
- A2B 在 `i == 1` 轮询 `MSTR_RUNNING`（INTTYPE 255）。前一条寄存器是 `0x13` 的 DELAY 轮询 `DSCDONE`（INTTYPE 24）。这张 829 条表的第一条 DELAY 就在下标 1，每次 `DISCVRY` 后面都跟着 DELAY。
- `val_width > 1` 会立刻返回。829 条的宽度和长度都是 1，这是失败即停，不是当前表上的缺陷。
- `Uart0Rd` 在缓冲空时增加 `underrun_cnt`。`MonTmp` 一直读到空，所以计数包含空闲轮询。`doc/adsp21593_framework.md` 已经这样写。
- CPU 负载 `cnt / 1000.f` 是相对单位，`doc/adsp21593_framework.md` 已说明。
- `tune.c` 的 `ack[]` / `nack[]` 没有被使用。命令 `0x05` 的 CRC16 不是 `0x436F`。想发的负载不清楚，所以没有改这两个常量。
- 没有 SPI 驱动。管脚复用只声明了 TWI2 和 UART0。音频走 SRU，不走 `PORT` 复用。工程里没有 `adi_pwr_Init`；时钟是复位默认值加上 codec PLL。
- `timer.c` 按 SCLK 125 MHz 估算时，除数 136 得到 125e6/136 ≈ 919118 baud，比 921600 低约 0.27%。这是最近的整数除数，不是缺陷。

### 构建

在本虚拟机上实际执行。仓库里没有 Makefile、CMakeLists 或 Python 构建脚本。

```text
$ make -C /workspace
make: *** No targets specified and no makefile found.  Stop.

$ cmake -S /workspace -B /tmp/adsp-build
CMake Error: The source directory "/workspace" does not appear to contain CMakeLists.txt.

$ command -v cc21k cces
cc21k: not found
cces: not found
```

存在 `/usr/bin/make`、`cmake`、`python3`、`gcc`。没有 `/opt/analog`。没有安装 CrossCore Embedded Studio，所以 `cc21k` 没有运行。把 `ShtLog` 截断逻辑抄到临时文件后，`gcc -Wall -Wextra` 编译通过并打印 `shtlog ok`。该文件不在仓库里。

固件构建需要：带 SHARC 工具链的 CrossCore Embedded Studio，处理器 ADSP-21593，硅版本 0.0，CCES 工程 `sharc1`（CORE1）和 `sharc2`。链接脚本是 `system/startup_ldf/app.ldf`。`sharc1/.cproject` 里的 loader kernel 是 Windows 路径 `D:\cces\SHARC\ldr\_prom.dxe`，换机器要改。ADI 驱动包需要 uart、twi、sport、tmr、spu、sru、sec、int。
