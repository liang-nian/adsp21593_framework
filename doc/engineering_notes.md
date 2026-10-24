# 工程笔记

英文摘要：SHARC1 跑音频与控制面，SHARC2 是空循环。控制总线是 TWI2（I2C）。音频时钟由 ADAU1962 产生，经 SRU 扇出。A2B 配置是 SigmaStudio 导出的命令表，由 `Ad2428Cfg()` 回放。使用 USBi 时必须先有 BCLK/FS，并且固件不能占用 TWI2。

本文按代码写。寄存器位能对上数据手册的会标明；对不上的只写代码里的值。

---

## 1. 总体架构

### 1.1 芯片与工程

目标是 ADSP-21593（双 SHARC+），板卡为 EV-21593-SOM + EV-SOMCRR-EZKIT。CCES 里：

| 工程 | 编译宏 | 角色 |
|------|--------|------|
| `sharc1/` | `-DCORE1` | 启动核。`main` 在 `sharc1/src/sharc1.c` |
| `sharc2/` | `-DCORE2` | 第二核。`main` 只调用 `adi_initComponents()` 然后空循环 |

`sharc1.c` 的 `adi_core_enable(ADI_CORE_SHARC1)` 是 CCES 模板里「Core1 拉起 Core2」的调用。本仓库的文件夹名 `sharc1` 指的是 CCES Core 1，不是「不要再使能的那个核」。

两个工程都把 MCAPI 编进 `adi_initComponents()`。应用代码没有收发 MCAPI 消息，第二核也没有音频任务。

启动顺序（SHARC1）：

1. CCES 启动代码、LDF（`sharc1/system/startup_ldf/app.ldf`）。
2. `adi_initComponents()`：`adi_sec_Init()` → `adi_initpinmux()` → `adi_mcapi_Init()`。
3. `adi_core_enable(ADI_CORE_SHARC1)`。
4. `AudioFlowInit()`。
5. `while (1) MonTmp();`

引脚复用（`pinmux_config.c`）只打开 TWI2 和 UART0：

| 信号 | 引脚 | FER | MUX |
|------|------|-----|-----|
| UART0_TX | PA06 | bit 6 | 功能 1 |
| UART0_RX | PA07 | bit 7 | 功能 1 |
| UART0_RTS | PA08 | bit 8 | 功能 1 |
| UART0_CTS | PA09 | bit 9 | 功能 1 |
| TWI2_SCL | PA14 | bit 14 | 功能 0 |
| TWI2_SDA | PA15 | bit 15 | 功能 0 |

SPORT 的 BCLK/FS/数据不走 GPIO MUX，走 DAI 和 SRU。

### 1.2 模块怎么挂在一起

```text
Spu0Init
  Uart0Init          RX DMA 先启动
    Tmr0Init         1 ms 中断里 Uart0Poll
      Sw0Init        U6 @ 0x22，打开 codec 复位脚
        SruInit      DAI 扇出 BCLK/FS 和数据
          Adau19620Init   DAC 锁相，成为 BCLK/FS 主机
            Ad2428ClkReady(pll_ok)
            Adau19790Init ADC，串行口从机
              Ad24280Init 仅在时钟就绪且 USBi 未占用 TWI 时回放脚本
                SpInit    仅在 DAC 锁相后启动六路 SPORT DMA
```

实时路径：SPORT 全局组 DMA 完成 → `SpGroupCallback()` 做 int32↔float → `adi_sec_Raise(INTR_TRU0_INT4)` → `AudioProcess()`。

`AudioProcess()` 按 channel-major 下标 `ch * FRAME_LEN + i` 工作（`FRAME_LEN = 48`）：

| DAC 通道 | 内容 |
|----------|------|
| 0–3 | ADC 同号回环 |
| 4–7 | 1 kHz 正弦（相位增量 `1000/48000`） |
| 8–11 | `a2b0_in` 的 slot 0、2、1、3 |

控制路径有两套，不要混：

- `MonTmp()` 读裸字节：`0xAA` A2B 监视、`0xBB` UART 计数、`0xCC` CPU 负载。
- `TuneRx()` / `TuneTx()` 是带帧头和 CRC 的协议，目前没有从 `main` 调用。

内存：UART 和 SPORT 的 DMA 缓冲、描述符放在 `seg_l1_block1`，并 `ADI_CACHE_ALIGN`。float 处理缓冲 `SpMsg` 在默认段，不在这条 pragma 里。

### 1.3 本地构建

仓库没有 Makefile / CMake。构建入口是 CCES 工程 `sharc1/.cproject`、`sharc2/.cproject`。

前提：

- CrossCore Embedded Studio，带 ADSP-21593 器件支持。工程里的生成文件落款到 2026 年，需要与之匹配的较新 CCES。
- SHARC 编译器 `cc21k`，`-proc ADSP-21593`，`-si-revision 0.0`。
- 器件和驱动头：`sys/platform.h`、`sys/adi_core.h`、`sru.h`、`services/int`、`services/spu`、`services/tmr`、`services/mcapi`、`drivers/twi`、`drivers/uart`、`drivers/sport`。这些不在本仓库里，由 CCES 安装目录提供。
- 包含路径（已写在 `.cproject`）：`sharc1/system`、`sharc1/src/algo`、`sharc1/src/driver`。
- 链接脚本：`sharc1/system/startup_ldf/app.ldf`（Core2 用自己的那份）。
- 源码根是整个 `src/` 和 `system/`。`tune.c`、`log.c` 都会进链接。

在 CCES 里：导入两个工程，先编 `sharc1` Debug（或 Release），再编 `sharc2`。启动核是 Core1。Loader 页的 kernel 文件写成了 `D:\cces\SHARC\ldr\_prom.dxe`，只在生成 ldr 时用到；换机器要改成自己的 CCES 安装路径。链 dxe 不读这个路径。

本次云端环境没有 `cces` / `cc21k`，没有生成 dxe。对不依赖 ADI 头的源文件做了主机语法检查（`gcc -std=c11 -Wall -Wextra -c`，包含路径同上），退出码 0。`asm("nop;")` 仅在主机 gcc 上告警。

---

## 2. 驱动协议与总线协议

代码里没有 SPI 驱动，也没有软件实现的 I2S 移位器。音频串行口是 SPORT 多通道模式，物理上是 TDM。I2C 由 TWI2 主机完成。UART0 是调试口。

### 2.1 UART0

实现：`Uart0Init`、`Uart0Poll`、`Uart0Rd`、`Uart0Wr`（`driver/uart/uart.c`）。

| 参数 | 代码 | 含义 |
|------|------|------|
| 外设号 | `UART0_DEV = 0` | UART0 |
| `adi_uart_ConfigBaudRate` 预分频 | 1 | 与除数相乘后分 SCLK |
| 除数 | 136 | 注释标称 921600。按 SCLK = 125 MHz（与 TMR0 同一假设）且 `baud = SCLK / (prescale * div)` 计算，得 125000000/136 ≈ 919118，相对 921600 约 −0.27% |
| RX DMA 长度 | 512 | 环形描述符，`pNxtDscp` 指向自己 |
| TX | 停模式，单描述符 | `XCount` 为本次字节数，`XModify = 1` |
| 轮询周期 | TMR0 1 ms | 见 2.2 |

线控（字长、校验、停止位）没有在本仓库里改。格式就是 `adi_uart_Open(..., ADI_UART_DIR_BIDIRECTION, ...)` 之后驱动留下的默认值。

RX 没有字节时钟。生产者不是「收满一帧」中断，而是：

1. DMA 按剩余 `XCNT` 往 `uart0_rx_buf` 写。
2. 每 1 ms `Uart0Poll()` 把剩余计数换成写指针：`wr = (xcnt == 0) ? 0 : 512 - xcnt`。
3. `Uart0Rd()` 用读指针取一字节。`rd == wr` 表示空，返回 1。

写指针是「下一格将要写入的位置」。从旧写指针走到读指针的距离就是还能写而不盖住未读数据的字节数。本拍写入量达到这个距离就记 `overrun_cnt`。921600 约 92 字节/ms，小于 512，一次 1 ms 轮询本身绕不完一整圈；主循环若长时间关中断，这个判断仍然成立。

TX：`is_tx_over` 初值 1。`Uart0Wr` 清 0，拷贝到 `uart0_tx_buf`，`adi_uart_DMAWrite(..., ADI_PDMA_STOP_MODE)`。回调 `ADI_UART_EVENT_TX_PROCESSED` 再置 1。超过 512 字节按块发送，每块单独用 `TIME_BND = 0x1000000` 次空转做超时。

`ShtLog()` 用 `vsnprintf` 写到 128 字节栈缓冲（含结尾 0），再调用 `Uart0Wr`。截断或编码失败返回 1。

监视口还有一层裸字节，没有帧头：`MonTmp()` 把读到的字节交给 `MonProcess()`。

| 字节 | 动作 |
|------|------|
| 0xAA | `Ad24280Mon()`，读 INTSRC / INTTYPE / BECNT |
| 0xBB | 打印 overrun / underrun 计数 |
| 0xCC | 打印 `CpuMsgMon()` |
| 其它 | 丢掉 |

### 2.2 调参帧（TuneRx / TuneTx）

这层目前没有挂到主循环。格式以 `tune.c` 的解析器为准。

| 字段 | 宽度 | 含义 |
|------|------|------|
| 头 | 2 | 固定 `0x5A 0xA5`。连续的 `0x5A` 留在「等第二个头字节」状态。头不进 CRC |
| cmd | 1 | 见下表 |
| len | 1 | 仅 cmd = 0x00。0 表示没有 payload，直接进入 CRC |
| payload | len | 仅 0x00，且 `1..255`。上限常量是 512，一字节长度实际到不了 |
| CRC16 | 2 | Modbus，初值 `0xFFFF`，低字节先发。覆盖 cmd、以及 0x00 的 len 和 payload |

| cmd | 帧内是否有 len | `CrcProc` 标志 |
|-----|----------------|----------------|
| 0x00 | 有 | `test_flag` |
| 0x01 | 无 | `a2b_mon_flag` |
| 0x02 | 无 | `uart_mon_flag` |
| 0x03 | 无 | `cpu_load_mon_flag` |
| 其它 | — | 回到等帧头，不应答 |

CRC 正确时再发 ACK：`5A A5 05 7F 43`。错误时发 NACK：`5A A5 06 3F 42`。这两个 CRC 是命令字节单独算出来的。`TuneProc()` 仍只清标志，不做回写或监视。

### 2.3 TWI2 / I2C

实现：`driver/twi/twi.c`。一个主机，7 位地址，每个驱动自己 `Twi2Open` / `Twi2Stop`。

| 参数 | 值 | 含义 |
|------|----|------|
| 控制器 | TWI2 | 引脚 PA14/PA15 |
| 模式 | `ADI_TWI_MASTER` | 片上是唯一主机（USBi 插入时这条不成立，见第 3 节） |
| 比特率 | 100 | 单位 kHz，传给 `adi_twi_SetBitRate` |
| 占空比 | 50 | 百分比 |
| 预分频 | 12 | 注释写 `sclk/10 MHz`。TMR0 假设 SCLK = 125 MHz，则内部时基约 125/12 ≈ 10.42 MHz，靠近手册要求的 10 MHz |

已用的 7 位地址：

| 地址 | 器件 | 访问 |
|------|------|------|
| 0x22 | U6，板级开关（寄存器布局与 MCP23017 BANK=0 一致） | 只写 |
| 0x04 | ADAU1962 | 写后读回 |
| 0x11 | ADAU1979 | 写后读回 |
| 0x68 | AD2428 主节点 | 脚本 |
| 0x69 | 当前被 `NODEADR` 选中的 A2B 从节点 | 脚本 |

短写 `Twi2DevRegShtWr`：`START + 地址/W + 寄存器 + 数据 + STOP`。两字节缓冲，一次 `adi_twi_Write(..., false)`。

短读：`START + 地址/W + 寄存器`，写调用的最后一个参数为 `true`（Repeated START，不发 STOP），然后 `地址/R + 1 字节 + STOP`。

长写 `Twi2DevRegLngWr`：寄存器地址按大端拆成 1、2 或 4 字节，后面拼接 payload，一次写，最后 STOP。不在地址和数据之间插入 Repeated START。当前 A2B 表不使用这条路径。

`Twi2Open()` 已占用时返回 1，不嵌套第二次 `adi_twi_Open`。传输函数在会话未打开时直接返回。

U6 写入顺序（`switch0_reg.c`）先写输出锁存再写方向，避免切成输出的瞬间出现毛刺。

使能阶段端口 A = `0x05`：bit7 `~ADAU1979_EN`、bit6 `~ADAU1962_EN`、bit5 `~ADAU_RESET` 都是 0，芯片上电并保持复位。复位释放阶段端口 A = `0x25`，bit5 变为 1，低有效复位脚放开。端口 B 两次都是 `0xFD`。方向寄存器 IODIRA = `0x18`，IODIRB = `0x00`。

### 2.4 SPORT / TDM

常量在 `sport.h`：`FRAME_LEN = 48`，`TDM_NORM = 16`，`TDM_A2B = 32`。48 kHz 下一块 DMA 是 1 ms。

TDM16、32 位槽时，位时钟为 `32 × 16 × 48000 = 24.576 MHz`。A2B 侧不是一根 32 槽的线，而是两根 TDM16 数据线，软件把它们收成 32 个槽。

`adi_sport_ConfigClock(handle, 32, internal, edge, gated)` 在本工程的六个调用里，`internal` 全是 `false`：SPORT 不自己产生位时钟。`gated` 全是 `false`。`edge` 按半 SPORT 不同，SP1B 相对 SP1A 错开，用来换 A2B 回程的建立时间。`sport.md` 里的表就是这些调用的第 4 个参数。

`adi_sport_ConfigData(..., ADI_SPORT_DTYPE_SIGN_FILL, 31, false, false, false)`：符号扩展，字长字段 31（配合 32 位槽）。

`adi_sport_ConfigFrameSync(..., 31, false, false, false, true, false, true)`：帧同步宽度 31，early FS 打开。DAC / ADC / A2B 的寄存器注释同样要求 early、上升沿脉冲帧同步、下降沿采样。

多通道：

| 半 SPORT | 方向 | 槽 | DMA 通道参数 | 对端 |
|----------|------|----|--------------|------|
| SP0A | TX | 0..15 | `ADI_SPORT_CHANNEL_PRIM` | DAI1 PB01 → DAC |
| SP0B | RX | 0..15 | `PRIM` | DAI1 PB06 ← ADC |
| SP1A | TX | 0..31 | `PRIM_SEC` | DAI0 PB05 = AD0，PB06 = AD1 → A2B |
| SP1B | RX | 0..31 | `PRIM_SEC` | DAI0 PB01 → BD0，PB02 → BD1 ← A2B |
| SP2A/B | TX/RX | 0..31 | `PRIM_SEC` | DAI1 上的第二路布线；板上没有第二片 A2B |

DMA 描述符两级乒乓，`XModify = 4`（32 位），`XCount = FRAME_LEN × 槽数`。全局组 `adi_sport_CreateGlobalGroup` 之后先 `GlobalEnable(false)`，再逐路 Enable，最后 `GlobalEnable(true)`，让六路对齐启动。

缓冲布局：DMA 是 time-major，`buf[i * TDM + j]`。`SpMsg` 的 float 是 channel-major，`ch * FRAME_LEN + i`。转换用 `__builtin_conv_FtoR` / `__builtin_conv_RtoF`。

组回调用 `cnt` 在 Buf0 和 Buf1 之间交替，两次都会拉起 `INTR_TRU0_INT4`。CPU 负载是这段回调到 `AudioProcess` 返回的 `__builtin_emuclk()` 差值除以 1000，再做最大值和系数 0.001 的指数平均。

### 2.5 SRU

`SruInit()`（`driver/sru/signal_route.c`）先把 DAI0/DAI1 输入使能写成 `0xFFFFF`。

时钟源是 DAC：

| 信号 | 源 | 去向 |
|------|----|------|
| BCLK | DAI1 PB05 输入（`PBEN = 0`） | SP0/1/2 的 A/B 时钟；DAI1 PB12 → ADC；DAI0 PB03 → A2B0；DAI1 PB08 → 遗留的 A2B1 布线 |
| FS | DAI1 PB04 输入 | 同样扇出到 SP0/1/2、ADC（DAI1 PB20）、A2B0（DAI0 PB04）、遗留 A2B1（DAI1 PB09） |

数据脚的 `PBEN`：输出为 1，输入为 0。SP0 各一根数据线。SP1/SP2 各两根，对应主数据口和次数据口。

### 2.6 ADAU1962（DAC，时钟主机）

I2C `0x04`。PLL 表然后配置表，都在同一次 TWI 会话里。

PLL 序列（`adau19620_pll`）：

| 寄存器 | 值 | 作用 |
|--------|----|------|
| 0x00 `PLL_CLK_CTRL0` | 0x01，随后 0x05 | 先写 0x01 再写 0x05，中间有 `TIME_BND` 空等 |
| 0x01 `PLL_CLK_CTRL1` | 0x22 | 之后轮询 bit 2。该位只读，1 = PLL 已锁。超时返回失败，调用方不再写配置表，也不允许启动 A2B/SPORT |

配置表里和串口直接相关的字节：

| 寄存器 | 值 | 代码注释 |
|--------|----|----------|
| `DAC_CTRL1` 0x07 | 0x43 | MSB first，BCLK 下降沿采样，主机，32 bit |
| `DAC_CTRL2` 0x08 | 0x00 | 256× 过采样 |
| `DAC_CTRL0` 0x06 | 先 0x01 后 0x20 | 0x20：TDM16、48 kHz、通道打开 |

每个配置字节写完立刻读回，不相等则只打印，不中止。音量字节为 0x00。`DAC_PWRx = 0xAA`。掉电寄存器在表尾被写成 0，通道退出掉电。

### 2.7 ADAU1979（ADC，串行从机）

I2C `0x11`。

PLL：

| 寄存器 | 值 | 作用 |
|--------|----|------|
| `POWER` 0x00 | 0x01 | 上电 |
| `PLL` 0x01 | 0x03 | 之后轮询 bit 7，1 = 锁定 |

串口相关：

| 寄存器 | 值 | 代码注释 |
|--------|----|----------|
| `BLOCK_POWER_SAI` 0x04 | 先 0x70 后 0x7F | 帧同步上升沿脉冲，BCLK 上升沿发送；0x70 静音，0x7F 打开 |
| `SAI_CTRL0` 0x05 | 0x23 | 48 kHz，TDM16 |
| `SAI_CTRL1` 0x06 | 0x08 | |
| `CMAP12/34` | 0x01 / 0x23 | 通道映射 0、1、2、3 |
| `POST_ADC_GAINx` | 0xA0 | 四路相同 |

ADC 的锁相失败只打印，不阻止 DAC 时钟已经建立后的 A2B。ADC 不是板级位时钟源。

### 2.8 SPU 与 TMR0

`Spu0Init()` 对 9 个 ID 调用 `adi_spu_EnableMasterSecure(..., true)`：SPORT 半口 63–68，UART0 = 29，UART0 RX DMA = 79，TX DMA = 80。必须在 UART/SPORT DMA 之前。

TMR0：设备 0，时钟 `ADI_TMR_CLKIN_SYSCLK`，连续 PWM，周期中断。`Period = 125000`。注释写明 SCLK = 125 MHz，故周期 1 ms。`Width = Period >> 2`。回调里只调用 `Uart0Poll()`。

### 2.9 A2B 寄存器访问（协议层）

物理层是 A2B；对 SHARC 而言主节点就是 I2C 从机 0x68。从节点不直接挂在 TWI2 上。主机先写主节点 `NODEADR`（0x01），再访问 0x69，主节点把这次传输转发到被选中的从节点。

`Ad2428Reg` 每个字段：

| 字段 | 宽度 | 含义 |
|------|------|------|
| `dev` | 8 | 7 位 I2C 地址。延时条目为 0 |
| `op_code` | 8 | 0 写，1 读，2 延时 |
| `reg_width` | 8 | 本表恒为 1 |
| `reg` | 32 | 寄存器地址。延时条目为 0。发现用 0x13，控制用 0x12 |
| `val_width` | 8 | 本表恒为 1。大于 1 会中止脚本 |
| `val_len` | 16 | 本表恒为 1 |
| `val_ptr` | 指针 | 指向 1 字节常量 |

主节点上脚本会碰、且固件自己也读的寄存器：

| 地址 | 名称 | 固件用法 |
|------|------|----------|
| 0x01 | NODEADR | 脚本在每次访问从节点前写入 |
| 0x12 | CONTROL | 0x84 启动主机；0x81 = MSTR\|ENDDSC；表尾 0x82 再次改结构 |
| 0x13 | DISCVRY | 写入响应周期，后面必须出现 DSCDONE |
| 0x15 | INTSTAT | 头文件有定义，运行时代码不读 |
| 0x16 | INTSRC | 非 0 表示有中断。读它再读 INTTYPE |
| 0x17 | INTTYPE | 255 = 主机已运行；24 = 发现完成；0..6 在监视函数里算数据错误 |
| 0x1A | INTPND2 | 脚本写 0x01 |
| 0x1F | BECNT | 监视时读出累加，再写 0 |

监视函数每次还会打印 `node_num`，这个数只在发现成功时加 1。

---

## 3. A2B 配置方法

### 3.1 SigmaStudio 产物怎么进工程

拓扑在 SigmaStudio 里搭好并经过 USBi 验证之后，导出命令表，收成 `Ad2428Reg ad24280_cfg[829]`（`ad24280_reg.c`）。固件不解析 SigmaStudio 工程文件，只回放这张表。

`Ad2428Cfg()` 的规则：

| op | 行为 |
|----|------|
| WRITE | `val_len == 1` 且 `reg_width == 1` 时短写，否则长写 |
| READ | 短读。读到的值丢弃。INTTYPE 数组里的 `0x00` 是占位，不是期望值 |
| DELAY | `val_ptr[0]` 作为空转倍数（`× 0x100000`）。本表没有 2 字节延时 |

两条和导出顺序绑在一起的等待：

1. 下标 1、且上一条是 `CONTROL`：空等之后轮询，直到 `INTSRC != 0` 且 `INTTYPE == 255`。这对应表头 `CONTROL = 0x84` 加 25（0x19）的延时。超时则整表中止并释放 TWI。
2. 上一条寄存器是 `0x13`：空等之后轮询 `INTTYPE == 24`（DSCDONE），`node_num++`。超时则中止。本表有 12 次，响应周期依次为 70、66、62、58、53、49、45、41、37、33、29、25，每次延时字节是 50（0x32）。

读 `INTTYPE` 会清掉这次中断。等待循环已经把 255 或 24 读走，脚本后面那条 INTTYPE 读只是再读一次，返回值不使用。

表尾：`CONTROL = 0x81`（0x82 之前还有一次 0x81 和 1 个单位的延时），最后一条是 `CONTROL = 0x82`。

从节点类型在导出符号里是 `A2BDMIC0` … `A2BDMIC11`，每个节点一组厂商 ID / 产品 / 版本读，然后是槽位、I2S、PDM 和中断屏蔽。`ad2428_reg.h` 的注释写的是：12 个从节点、每节点 2 个麦、48 kHz、下降沿采样、FS 上升沿脉冲、early FS、TDM16、32 bit，收发器相对 SHARC 为 I2S 从机。

`ad24281_cfg` 只有一个全 0 元素。`Ad24281Init()` 只打印，不访问总线。官方评估板没有第二片 AD2428。

### 3.2 USBi：时钟必须先起来，TWI 不能被占用

USBi 是 I2C 主设备，接到同一条 TWI2。它不产生 BCLK/FS。A2B 主节点没有位时钟和帧同步时，PLL 不锁定，发现不会完成。USBi 插上之后即使发现失败也不会放开总线，片上 `adi_twi_Open` 不能和它同时当主机。

正确顺序：

1. **先时钟，后总线。** `SruInit()` 把 DAC 的 BCLK（DAI1 PB05）和 FS（DAI1 PB04）扇出到 A2B 的 DAI0 PB03/PB04。`Adau19620Init()` 必须看到 PLL lock。`Ad2428ClkReady()` 只有在这一步返回成功时才为 1。
2. **固件把 TWI 让出来。** 开关、DAC、ADC 的 init 都以 `Twi2Stop()` 结束。`Twi2Open()` 拒绝重入。
3. **USBi 会话不要调用 `Ad24280Init()`。** 把 `AD2428_USBI_OWNS_TWI` 设为 1 后编译。时钟就绪时该函数打印并返回，不会 `Twi2Open`。USBi 应在固件已经跑过 SRU 和 DAC、并且没有占着 TWI 之后再插入。上电前就插着 USBi，开关和 codec 的 `Twi2Open` 仍会和探头冲突。
4. SigmaStudio 发现或下载完成后拔掉 USBi，把宏改回 0，重新烧录。片上主机再回放 `ad24280_cfg`。
5. 回放失败（主机没起来，或某一跳没有 DSCDONE）立即 `Twi2Stop()`，不把剩余命令留在总线上。

只靠调试器停在 `Ad24280Init()` 前面、不改宏，也可以把 TWI 空出来，但停住的位置必须在 DAC 锁相之后、并且没有任何驱动还停在 `Twi2Open` 和 `Twi2Stop` 之间。宏是为了让这个顺序在重新运行时仍然成立。

`Ad24280Mon()` 仍会打开 TWI2。USBi 还插着的时候不要发 `0xAA`。
