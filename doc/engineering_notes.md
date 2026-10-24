# Architecture, buses, and A2B load order / 架构、总线与 A2B 加载顺序

**[English](#english)** | **[中文](#中文)**

Field values below are the ones in this tree (`sharc1/src`). Bit names for the AD242x and ADAU1979 registers follow the public Analog Devices documents those bytes were checked against. Where this firmware never programs a field, the note says so.

---

<a id="中文"></a>

## 中文

### §1 总体架构

目标是 **ADSP-21593**（双 SHARC+）裸机音频框架，板卡为 **ADZS-21593-EZKIT / SOM-21593 + SOMCRR-EZKIT**。官方评估板上只有 **一路 A2B 主节点 AD2428**（代码里的 AD24280，I2C `0x68` / `0x69`）。AD24281（`0x6C` / `0x6D`）是空桩，`ad24281_cfg` 只有一条全 0，`AudioFlowInit` 里 `Ad24281Init()` 保持注释。

两颗核：

| 核 | 工程 | 角色 |
|----|------|------|
| CCES Core 1 | `sharc1/` | 音频、驱动、主循环 |
| CCES Core 2 | `sharc2/` | `adi_initComponents()` 之后空转。没有驱动 |

启动（`sharc1/src/sharc1.c`）：

1. `adi_initComponents()`（`sharc1/system/adi_initialize.c`）：`adi_sec_Init()`，然后 `adi_initpinmux()`，然后 `adi_mcapi_Init()`。
2. `adi_core_enable(ADI_CORE_SHARC1)`。这是 CCES 模板：正在跑的 Core 1 打开第二颗 SHARC。`ADI_CORE_SHARC1` 这个枚举名指的是 Core 2，不是把本核再打开一次。
3. `AudioFlowInit()`。
4. `while (1) MonTmp();`。主循环只从 UART 取单字节监视命令。音频在 SPORT 组回调里经 `INTR_TRU0_INT4` 进 `AudioProcess`。

`AudioFlowInit`（`audio_flow.c`）的顺序是有依赖的，不能对调：

```text
Spu0Init → Uart0Init → Tmr0Init → Sw0Init → SruInit
        → Adau19620Init → Adau19790Init → Ad24280Init → SpInit
```

| 步骤 | 为什么在这个位置 |
|------|------------------|
| SPU | DMA 主设备（SPORT、UART）在 SPU 里放行之后才能访问。 |
| UART 先于 TMR | TMR0 的 1 ms 回调就是 `Uart0Poll`。RX DMA 必须已经 `adi_uart_DMARead`。 |
| TMR | `adi_tmr_Config` 把 125000 个 SCLK 周期写进定时器，然后才 Enable。 |
| switch | I2C GPIO 扩展（`0x22`）打开 codec 电源并释放低有效复位，再空等至少 300 ms。 |
| SRU | 把 DAC 的 BCLK/FS 扇出到全部 SPORT，以及 ADC、A2B 的时钟脚。此时线上还没有稳定时钟，因为 DAC PLL 还没配。 |
| ADAU1962 | TDM 时钟主机。PLL 锁定后 `Adau1962ClkReady()` 变为 1，BCLK/FS 才算可用。 |
| ADAU1979 | SAI 从机，吃上面的 BCLK/FS。 |
| AD24280 | 只有时钟已锁定且 TWI2 空闲才回放发现脚本。USBi 必须已经拔掉。 |
| SPORT | 最后开 DMA。六路半 SPORT 都是时钟从机。 |

管脚复用（`pinmux_config.c`）只声明了两组 PORT 功能，其余音频脚走 DAI/SRU，不经过 `PORTA_MUX`：

| 功能 | 引脚 | `PORTA_MUX` | `PORTA_FER` |
|------|------|-------------|-------------|
| UART0 TX | PA6 | bit12 = 1 | bit6 |
| UART0 RX | PA7 | bit14 = 1 | bit7 |
| UART0 RTS | PA8 | bit16 = 1 | bit8 |
| UART0 CTS | PA9 | bit18 = 1 | bit9 |
| TWI2 SCL | PA14 | bit28 = 0 | bit14 |
| TWI2 SDA | PA15 | bit30 = 0 | bit15 |

工程里没有 `adi_pwr_Init`，也没有 CGU 配置。`timer.c` 按 **SCLK = 125 MHz** 写死 1 ms 和 UART 除数；codec 的 MCLK 是板级 **24.576 MHz `AUDIO_CLK`**（512 × 48 kHz）。本树不改内核时钟。

模块怎么接在一起：

```text
ADAU1962 (I2C 0x04, BCLK/FS 主机)
    │  DAI1 PB05 = BCLK, DAI1 PB04 = FS
    │  SRU 扇出
    ├─ SPT0_A/BCLK, SPT0_A/BFS     SP0A → DAC 数据 DAI1 PB01
    ├─ 同上                        SP0B ← ADC 数据 DAI1 PB06
    ├─ SPT1_A/BCLK, SPT1_A/BFS     SP1A → A2B  DAI0 PB05/PB06（两根 TDM16）
    ├─ 同上                        SP1B ← A2B  DAI0 PB01/PB02
    ├─ DAI0 PB03 / PB04            ADC 的 BCLK / FS
    └─ DAI1 PB12/PB08, PB20/PB09   A2B 的 BCLK / FS

TWI2 (PA14/15, 100 kHz) 分时独占：switch 0x22 → DAC 0x04 → ADC 0x11 → A2B 0x68/0x69
UART0 (PA6/7, 921600) ← TMR0 1 ms 轮询 RX DMA
```

演示算法 `AudioProcess`：48 点一帧。DAC 通道 0–3 拷贝 ADC，通道 4–7 是 1 kHz 满幅正弦，通道 8–11 取 A2B 输入的槽 0/2/1/3（把两根 TDM16 上的相邻槽交错听出来）。缓冲是通道主序：`ptr[ch * FRAME_LEN + i]`。

### §2 本工程用到的驱动与总线

没有 SPI 驱动。`switch0_reg.c` 的注释里出现 MicroSD SPI、OCTAL_SPI_CS，那是 GPIO 扩展器上的板级使能脚名字，固件不去拨它们，也没有 SPI 控制器初始化。

#### UART0

`uart.c`。只做了这些编程：

| 项 | 值 | 物理含义 |
|----|----|----------|
| 设备 | `adi_uart_Open(0, ADI_UART_DIR_BIDIRECTION, …)` | UART0，全双工 |
| 预分频 | `UART0_PRESCALE = 1` | 交给 `adi_uart_ConfigBaudRate`。配合除数，按 SCLK 直接除，而不是再除 16 |
| 除数 | `UART0_DIV = 136` | 注释目标 921600。SCLK 125 MHz 时 125e6/136 ≈ 919118 baud，相对 921600 低约 0.27%，是最近的整数 |
| 字长 / 校验 / 停止位 | 未再编程 | 保持 `adi_uart` 打开后的默认：8 数据位、无校验、1 停止位 |
| RTS/CTS | 管脚已复用到 PA8/PA9 | 驱动没有打开硬件流控 |

RX：描述符指向自己，`XCount = 512`，`XModify = 1`，`ADI_PDMA_DESCRIPTOR_LIST`。循环 DMA 不停把字节写入 `uart0_rx_buf`。UART0 RX 在这颗片子上是 **DMA21**，剩余次数寄存器是 `pREG_DMA21_XCNT_CUR`。写指针：

```text
xcnt = min(XCNT_CUR, 512)
wr   = 512 - xcnt
若 wr == 512 则 wr = 0
```

`XCNT_CUR == 0` 表示这一圈刚好写完，下一个字节会落在下标 0，所以不能把 512 当作下标。

TX：停止模式 DMA，`XCount` 为本次长度，最长 512。完成事件 `ADI_UART_EVENT_TX_PROCESSED` 把 `is_tx_over` 置 1。`Uart0Wr` 拒绝空指针和 0 长度；超过 512 就分块，块与块之间等待完成；长度刚好是 512 的倍数时不再补一次 0 长度的 DMA。每次等待的超时是 `0x1000000` 次空转，独立重装。

`Uart0Poll` 由 TMR0 每 1 ms 调用。它只更新写指针和 overrun。`Uart0Rd` 在 `rd == wr` 时返回 1，并把 `underrun_cnt` 加 1。`MonTmp` 会一直读到空，因此空闲轮询也会计入 underrun，这是当前设计。

主循环实际协议是单字节，没有帧头（`mon.c` `MonProcess`）：

| 字节 | 动作 |
|------|------|
| `0xAA` | `Ad24280Mon()`，读中断和误码；`Ad24281Mon()` 是空函数 |
| `0xBB` | `Uart0Mon()`，打印 underrun / overrun 计数 |
| `0xCC` | `CpuMsgMon()`，打印 SPORT 回调里用 `emuclk` 差除以 1000 的相对负载 |
| 其它 | 丢掉 |

`tune.c` 里还有一套帧，**没有**接到 `MonTmp`，主循环不会解析它。格式按 `TuneRx` / `TuneTx` 的实现记在这里，避免以后再发出对不上的长度字段：

| 字段 | 宽度 | 含义 |
|------|------|------|
| 帧头 | 2 | 固定 `5A A5`。不进入 CRC |
| cmd | 1 | 见下表。CRC 从这一字节开始，初值 `0xFFFF`，算法是 Modbus CRC16 |
| len | 0 或 1 | 仅 cmd `0x01` 有 1 字节长度。`0x02`–`0x04` 没有长度 |
| payload | len | 仅 `0x01` 且 len > 0。最长受 `TuneRx` 的 512 缓冲限制；`TuneTx` 拒绝 len > 255 |
| CRC | 2 | 低字节在前，高字节在后 |

| cmd | `TuneRx` 之后的状态 | `CrcProc` |
|-----|---------------------|-----------|
| `0x01` | 收 1 字节长度和负载 | `test_flag` |
| `0x02` | 直接收 CRC | `a2b_mon_flag` |
| `0x03` | 直接收 CRC | `uart_mon_flag` |
| `0x04` | 直接收 CRC | `cpu_load_mon_flag` |
| 其它 | 回到等帧头 | 不处理 |

CRC 正确后 `TuneProc` 目前把标志清掉，没有真正上传参数。`ack[] = {5A A5 05 6F 43}` 和 `nack[] = {5A A5 06 FF 76}` 没有被发送，也没有按上面的 CRC 规则去改。

日志 `ShtLog` 把 `vsnprintf` 的结果送到 `Uart0Wr`。缓冲 128 字节，截断时只发 127 字节（不含结尾的 NUL）。

#### TWI2（I2C）

`twi.c`。控制器 **TWI2**，主机模式。

| 参数 | 值 | 物理含义 |
|------|----|----------|
| 预分频 | 12 | 注释：`sclk / 10 MHz`。ADI TWI 要用约 10 MHz 的内部时基再产生比特率。125 MHz / 10 MHz = 12.5，代码取 12 |
| 比特率 | 100 | kHz，标准模式 |
| 占空比 | 50 | 百分比，高低时间相等 |
| 地址 | 7 位 | `adi_twi_SetHardwareAddress` 不包含 R/W 位 |

线上三种事务：

**短写** `Twi2DevRegShtWr` / `Twi2RegShtWr`：一次 `adi_twi_Write`，2 字节，`repeat start = false`。

```text
START + ADDR(W) + reg + data + STOP
```

**短读** `Twi2DevRegShtRd`：先 `Write` 1 字节且 `repeat start = true`，再 `Read` 1 字节且 `repeat start = false`。

```text
START + ADDR(W) + reg + REPEATED START + ADDR(R) + data + STOP
```

**长写** `Twi2DevRegLngWr`：地址按大端展开（只允许 1、2 或 4 字节），后面紧跟数据，一次 `Write`，`repeat start = false`，数据最长 128 字节。不允许把地址和数据拆成两次寻址写，否则从机会把数据的第一字节当成寄存器地址。

```text
START + ADDR(W) + reg[MSB … LSB] + data[0 … n-1] + STOP
```

占用规则：`twi2_busy`。`Twi2Open` 在忙时直接返回，避免在同一块驱动内存上再 `adi_twi_Open`。`Twi2Stop` 在已空闲时直接返回。每个设备初始化都是 Open、访问、Stop。`Twi2IsIdle()` 供 A2B 在打开总线之前查询。USBi 插在同一对 SCL/SDA 上时，它是另一个主机，`twi2_busy` 看不出它；固件侧只能保证自己停干净，人必须把 USBi 拔掉。

| 从机 | 7 位地址 | 谁打开 TWI |
|------|----------|------------|
| GPIO 开关 U6 | `0x22` | `Sw0Init` |
| ADAU1962 DAC | `0x04` | `Adau19620Init` |
| ADAU1979 ADC | `0x11` | `Adau19790Init` |
| AD2428 主节点 | `0x68` | `Ad24280Init` / `Ad24280Mon` |
| AD2428 总线侧从节点 | `0x69` | 同一段 TWI 会话，脚本里的 `dev` 字段 |

codec 寄存器都是 1 字节地址 + 1 字节数据，走短写。写完 DAC 配置后会读回，不相等就 `ShtLog`。

#### 板级开关（仍是 I2C，不是独立总线）

`switch0_reg.c` 的寄存器号与 MCP23017 类扩展器一致：

| 寄存器 | 地址 | 本工程写入 |
|--------|------|------------|
| IODIRA | `0x00` | `0x18`。bit4、bit3 为输入（原理图上未使用），其余为输出。0 = 输出 |
| IODIRB | `0x01` | `0x00`。端口 B 全部输出 |
| GPIOA | `0x12` | 使能阶段 `0x05`，释放复位阶段 `0x25` |
| GPIOB | `0x13` | 两阶段都是 `0xFD` |

先写 GPIO 再写 IODIR，这样方向从复位默认的输入变成输出时，锁存器里已经是目标电平。

GPIOA bit7→bit0 与 `switch0_reg.c` 注释一致（低有效的脚用 `~` 标出）：

| bit | 脚 | 使能 `0x05` | 释放复位 `0x25` |
|-----|----|-------------|-----------------|
| 7 | `~ADAU1979_EN` | 0，打开 | 0，保持打开 |
| 6 | `~ADAU1962_EN` | 0，打开 | 0，保持打开 |
| 5 | `~ADAU_RESET` | 0，仍在复位 | 1，释放复位 |
| 4 | 未使用 | 0 | 0 |
| 3 | 未使用 | 0 | 0 |
| 2 | `~MicroSD_SPI` | 1，不启用 | 1 |
| 1 | `PUSHBUTTON_EN` | 0 | 0 |
| 0 | `EEPROM_EN` | 1 | 1 |

GPIOB `0xFD` = `1111 1101`：bit1 `~SPDIF_DIGITAL_EN` 为 0（打开数字 SPDIF 使能），其余保持注释里的不活动电平。固件没有 SPI 主机去跟这些片选说话。

释放复位并 `Twi2Stop` 之后，`SwResetSettle` 用 `__builtin_emuclk` 空转 3×10^8 周期。ADAU1962A Rev. A 要求 PU/RST 之后 300 ms。CCLK 最高按 1 GHz 估，这个等待不会短于 300 ms；核更慢时只会更长。

#### SPORT / I2S / TDM

采样率没有单独的 SPORT 寄存器。它由 DAC 的 FS 字段和帧长一起定死：**48 kHz**，`FRAME_LEN = 48`，一帧 1 ms。

| 项 | 值 | 物理含义 |
|----|----|----------|
| 时隙时钟 | 32 | `ConfigData(..., 31, ...)` 的字长字段是长度减 1，即 32 bit。`ConfigClock` 的 `nClockRatio = 32` |
| 数据格式 | `ADI_SPORT_DTYPE_SIGN_FILL`，随后三个 bool 都是 false | 符号扩展；MSB 在前；不打包；不右对齐 |
| 帧同步分频 | 31 | `ConfigFrameSync` 第 2 参。与 32 bit 时隙对齐 |
| 帧同步来源 | 第 3 参 false | 外部 FS，来自 DAC |
| early FS | 第 6 参 true | 帧同步比时隙 MSB 早一个 BCLK，与 codec / A2B 的 early 设置一致 |
| 多通道 | `ConfigMC(h, 1, 15 或 31, 0, true)` | 帧延时 1，窗口 15 表示 16 时隙、31 表示 32 时隙，窗口偏移 0 |
| 通道使能 | `SelectChannel(h, 0, 15)` 或 `(0, 31)` | 从时隙 0 连续打开 |
| DMA | `XModify = 4`，长度 `FRAME_LEN × 时隙数` | 每个样本 4 字节。描述符链表乒乓 |
| 模式 | `ADI_SPORT_MC_MODE` | 多通道 TDM，不是立体声 I2S 的 LRCLK 50% 模式 |

`adi_sport_ConfigClock(h, nClockRatio, bUseIntlClk, bRisingEdge, bGatedClk)`：

| 半 SPORT | 方向 | 时隙 | bUseIntlClk | bRisingEdge | bGatedClk | 数据脚 |
|----------|------|------|-------------|-------------|-----------|--------|
| SP0A | TX → DAC | 16 | false | true | false | DAI1 PB01 ← `SPT0_AD0` |
| SP0B | RX ← ADC | 16 | false | false | false | DAI1 PB06 → `SPT0_BD0` |
| SP1A | TX → A2B0 | 32 | false | false | false | DAI0 PB05/PB06 ← `SPT1_AD0/AD1` |
| SP1B | RX ← A2B0 | 32 | false | **true** | false | DAI0 PB01/PB02 → `SPT1_BD0/BD1` |
| SP2A | TX → A2B1 | 32 | false | true | false | DAI1 PB19/PB10。板上没有第二路 A2B |
| SP2B | RX ← A2B1 | 32 | false | true | false | DAI1 PB11/PB07 |

六路都是外部时钟。第 4 参是 CKRE（沿），不是内部时钟。SP1B 与 SP1A 的沿故意相反：A2B 往返加走线之后，24.576 MHz 下半个 bit 只有约 20 ns，同一沿关系容易吃掉建立/保持时间。门控时钟全关；多通道模式下驱动也会忽略它。

BCLK 频率：TDM16 × 32 bit × 48 kHz = **24.576 MHz**。DAC 作为主机从 24.576 MHz `AUDIO_CLK` 分出这个 BCLK 和 FS，SRU 再扇出。软件里的 “TDM32” 不是一根 32 时隙线。SRU 在 A2B0 每个方向都接了两根脚，SPORT 按每根 TDM16 来开多通道。A2B 脚本里上游 DTX 是两根脚，下游只开了 DRX0（见 §3）。DMA 仍把主数据口和副数据口排进一块 32 时隙缓冲（`TDM_A2B = 32`）。`ADI_SPORT_CHANNEL_PRIM_SEC` 用于 SP1/SP2，SP0 只有主数据口。

组回调在每次缓冲完成时做定点↔浮点，并 `adi_sec_Raise(INTR_TRU0_INT4)`。`AudioCallBack` 再跑 `AudioProcess`。

#### SRU 与 SPU

SRU（`signal_route.c`）不做协议，只接线。`DAI0_IE` 和 `DAI1_IE` 写成 `0xFFFFF`，DAI 输入使能打开。时钟源是输入脚，不是 SPORT 自己产生：

| 信号 | 源 | 去向 |
|------|----|------|
| BCLK | DAI1 PB05（DAC 输出，PBEN=0 表示该脚为输入） | SPT0/1/2 的 ACLK 与 BCLK；再驱动 DAI1 PB12、DAI0 PB03、DAI1 PB08 |
| FS | DAI1 PB04 | SPT0/1/2 的 AFS 与 BFS；再驱动 DAI1 PB20、DAI0 PB04、DAI1 PB09 |
| DAC 数据 | `SPT0_AD0` | DAI1 PB01 |
| ADC 数据 | DAI1 PB06 | `SPT0_BD0` |
| A2B0 TX | `SPT1_AD0`、`SPT1_AD1` | DAI0 PB05、PB06 |
| A2B0 RX | DAI0 PB01、PB02 | `SPT1_BD0`、`SPT1_BD1` |
| A2B1 TX/RX | SP2 主副数据口 | DAI1 PB19/PB10 与 PB11/PB07。硬件不存在 |

SPU（`spu.c`）对 9 个目标调用 `adi_spu_EnableMasterSecure(..., true)`：

| ID | 外设 |
|----|------|
| 63, 64 | SPORT0 A/B |
| 65, 66 | SPORT1 A/B |
| 67, 68 | SPORT2 A/B |
| 29 | UART0 |
| 79 | UART0 RX DMA（DMA21） |
| 80 | UART0 TX DMA（DMA20） |

#### 定时器

`timer.c`。TMR0，连续 PWM 输出模式，IRQ 在周期点，时钟源系统时钟，周期 **125000**。注释写明 SCLK 125 MHz，125000 / 125e6 = 1 ms。脉宽是周期的 1/4，延时 0，允许优雅停止。回调里只处理 `ADI_TMR_EVENT_DATA_INT` → `Uart0Poll`。`adi_tmr_Config` 必须在 Enable 之前，否则驱动用的是复位配置，回调不会按 1 ms 来。

#### ADAU1962 DAC（I2C 从机，TDM 主机）

地址 `0x04`。PLL 三步写在 `adau19620_pll`，中间各空转 `0xFFFF` 次 `nop`，然后读 `PLL_CLK_CTRL1`（`0x01`）直到 **bit 2（PLL_LOCK，只读）** 为 1。写入值 `0x22` 的 bit 2 本身是 0，锁定位是片子自己置上的。超时约 `0xFFFF` 次读，失败只打日志，`adau1962_clk_ready` 保持 0，后面的 A2B 初始化会因此退出。

| 顺序 | 寄存器 | 值 | 含义 |
|------|--------|----|------|
| PLL 1 | `PLL_CLK_CTRL0` `0x00` | `0x01` | 先按复位附近的值上电 |
| PLL 2 | `PLL_CLK_CTRL0` | `0x05` | 最终档。24.576 MHz / 48 kHz = 512，对应该片 MCS 的 512×fs |
| PLL 3 | `PLL_CLK_CTRL1` `0x01` | `0x22` | 启动 PLL。bit 2 留给锁定状态 |
| 掉电 | `PDN_CTRL_1/2/3` | `0x00`, `0xFF`, `0x0F` | 先按表把通道保持在掉电 |
| 串口 | `DAC_CTRL0` `0x06` | 先 `0x01`，最后 `0x20` | `0x20`：SAI=`100` TDM16，FS=`00` 即 48 kHz |
| 串口 | `DAC_CTRL1` `0x07` | `0x43` | 代码注释：MSB 在前、BCLK 下降沿采样、主机、32 bit |
| 串口 | `DAC_CTRL2` `0x08` | `0x00` | 代码注释：256× OSR |
| 静音 / 音量 | `0x09`–`0x17` | `0x00` | 不静音，主音量和 DAC1–DAC12 音量为 0 dB 码 |
| 驱动 | `PAD_STRGTH` `0x1C` | `0x00` | 默认驱动强度 |
| 电源 | `DAC_PWR1/2/3` | `0xAA` | 各通道电源位按 `10101010` |
| 退出掉电 | `PDN_CTRL_2/3` | `0x00`, `0x00` | 通道上电。这一步在最终 `DAC_CTRL0 = 0x20` 之前 |

DAC 是 BCLK/FS 的唯一主机。SPORT、ADC、A2B 都是从机。

#### ADAU1979 ADC（I2C 从机，SAI 从机）

地址 `0x11`。PLL：`M_POWER (0x00) = 0x01`（上电），`PLL_CONTROL (0x01) = 0x03`。bit 7 是 PLL_LOCK（只读，轮询用）；低位 MCS=`011` 对应 32–48 kHz 频段的 512×fs，和 24.576 MHz MCLK 一致。

`SAI_CTRL0 (0x05) = 0x22`：

| 位 | 字段 | `0x22` | 含义 |
|----|------|--------|------|
| [7:6] | SDATA_FMT | `00` | I2S 式：数据比 LRCLK 边沿晚 1 个 BCLK。TDM 脉冲模式下与 early FS 一起用 |
| [5:3] | SAI | `100` | TDM16 |
| [2:0] | FS | `010` | 32–48 kHz。`0x23` 会把 FS 写成 `011`（64–96 kHz），已经改掉 |

`SAI_CTRL1 (0x06) = 0x08`（只有 bit 3 为 1）：

| 位 | 字段 | 值 | 含义 |
|----|------|----|------|
| 7 | SDATA_SEL | 0 | 数据从 SDATAOUT1 出 |
| [6:5] | SLOT_WIDTH | `00` | 每时隙 32 个 BCLK（TDM16 × 32 = 512×fs） |
| 4 | DATA_WIDTH | 0 | 24 bit 数据。该位为 1 才是 16 bit |
| 3 | LR_MODE | 1 | 帧同步是一个 BCLK 宽的脉冲。为 0 才是 50% LRCLK |
| 2 | SAI_MSB | 0 | MSB 在前。为 1 才是 LSB 在前 |
| 1 | BCLKRATE | 0 | 主机模式下每通道 32 个 BCLK。本片是从机，此时钟由外部给出 |
| 0 | SAI_MS | 0 | LRCLK/BCLK 从机 |

`BLOCK_POWER_SAI (0x04)` 先 `0x70` 再 `0x7F`：bit 6 BCLKEDGE=1（按上升沿发数据）、bit 5 LDO、bit 4 VREF 先开；`0x7F` 再打开四个 ADC 通道。`CMAP12 = 0x01`、`CMAP34 = 0x23` 相对复位值 `0x10` / `0x32` 对调了相邻通道，算法按这个映射听，没有改回。后级增益四个通道都是 `0xA0`（复位默认，0 dB）。过温驱动 `0xF0`。`MISC_CONTROL = 0x00`。

### §3 A2B 配置与加载顺序

#### 脚本从哪来

`ad24280_reg.c` 是 SigmaStudio 导出的命令表，类型为 `Ad2428Reg`（`ad2428_reg.h`）。拓扑注释：12 个从节点，每节点 2 个麦，48 kHz，下降沿采样，FS 上升沿脉冲，early FS，TDM16，32 bit 时隙，A2B 相对 SHARC 是 I2S/TDM 从机。

每条命令 7 个字段：

| 字段 | 类型 | 本表取值 | 含义 |
|------|------|----------|------|
| `dev` | uint8 | `0x68` 158 条，`0x69` 656 条，`0x00` 15 条 | I2C 地址。`0x68` 是主节点本地口，`0x69` 是主节点转发到总线侧从节点的口。`0x00` 只出现在 DELAY |
| `op_code` | uint8 | WRITE `0` 765 条，READ `1` 49 条，DELAY `2` 15 条 | 见下方回放规则 |
| `reg_width` | uint8 | 全部 `1` | 寄存器地址字节数 |
| `reg` | uint32 | 单字节地址，放在低 8 位 | DELAY 的 `reg` 为 0 |
| `val_width` | uint8 | 全部 `1` | 大于 1 时 `Ad2428Cfg` 立刻返回。本表不会触发 |
| `val_len` | uint16 | 全部 `1` | 数据或延时的字节数 |
| `val_ptr` | uint8* | 指向 1 字节静态数组 | WRITE/DELAY 的载荷。READ 的指针内容不被写回 |

`Ad2428Cfg` 的回放：

| op | 动作 |
|----|------|
| WRITE，且 `val_len == 1`、`reg_width == 1` | `Twi2DevRegShtWr(dev, reg, *val_ptr)` |
| 其它 WRITE | `Twi2DevRegLngWr`。本表没有这种条目 |
| READ | `Twi2DevRegShtRd`。返回值丢掉。读 `INTTYPE` 会清除中断，这是脚本里的读 |
| DELAY | 忙等 `delay × 0x100000`。1 字节用 `val_ptr[0]`；若将来导出 2 字节，按大端 `(b0<<8)\|b1`。空指针当 0 |

两条与这张表绑在一起的轮询（换一张表之前不能只改条件）：

- 下标 `i == 1` 的 DELAY（本表第一条 DELAY，值 `0x19` = 25）之后，读主节点 `INTSRC (0x16)` 和 `INTTYPE (0x17)`，直到 INTTYPE 为 **255 `MSTR_RUNNING`**（主节点 PLL 锁定）。超时 `0x100000` 次打日志并继续。
- 若上一条命令的寄存器是 **`0x13` DISCVRY**，这条 DELAY 之后等到 INTTYPE 为 **24 `DSCDONE`**。每成功一次 `node_num` 加 1。

12 次发现的 `DISCVRY` 载荷（响应周期，逐级变小）：

```text
0x46, 0x42, 0x3E, 0x3A, 0x35, 0x31, 0x2D, 0x29, 0x25, 0x21, 0x1D, 0x19
```

15 个延时字节：`0x19`，`0x01`，然后 12 个 `0x32`（每次发现后 50），最后 `0x01`。

主节点 `CONTROL (0x12)` 的三次写，位定义 bit7→bit0 为 `MSTR, Reserved, Reserved, XCVRBINV, SWBYP, SOFTRST, ENDDSC, NEWSTRCT`：

| 值 | 位 | 何时 |
|----|----|------|
| `0x84` | MSTR \| SOFTRST | 表的第一条。成为主节点并软复位协议引擎 |
| `0x81` | MSTR \| NEWSTRCT | 软复位之后，以及时隙结构写完之后。把新结构同步到已发现的节点 |
| `0x82` | MSTR \| ENDDSC | 表的最后一条。结束发现 |

12 个从节点的 `CONTROL` 写的是 `0x00`（保持从节点）。`XCVRBINV` 没有置位，脚本按交叉线而不是直通线。

与 SHARC 时序直接对应的主节点寄存器（都在第一次 `DISCVRY` 之前写入；TRM 要求 `I2SGCFG` 先于发现）：

**`I2SGCFG (0x41) = 0x44`**，bit7→bit0：`INV, EARLY, ALT, TDMSS, RXONDTX1, TDMMODE[2:0]`

| 位 | 字段 | 值 | 含义 |
|----|------|----|------|
| 7 | INV | 0 | SYNC 上升沿对应通道 0 |
| 6 | EARLY | 1 | SYNC 比通道 0 的 MSB 早一个 BCLK |
| 5 | ALT | 0 | SYNC 是 1 个 BCLK 宽的脉冲，不是 50% 方波 |
| 4 | TDMSS | 0 | 时隙 32 bit（为 1 才是 16 bit） |
| 3 | RXONDTX1 | 0 | DTX1 不改作第三路 PDM/接收 |
| [2:0] | TDMMODE | `100` = 4 | TDM16 |

**`I2SCFG (0x42) = 0x93`**，bit7→bit0：`RXBCLKINV, RX2PINTL, RX1EN, RX0EN, TXBCLKINV, TX2PINTL, TX1EN, TX0EN`

| 位 | 字段 | 值 | 含义 |
|----|------|----|------|
| 7 | RXBCLKINV | 1 | DRX0/DRX1/SYNC 在 BCLK **下降沿**采样 |
| 6 | RX2PINTL | 0 | 两根接收脚不交错；低半时隙在 DRX0，高半在 DRX1。这里只开了 DRX0 |
| 5 | RX1EN | 0 | DRX1 关闭 |
| 4 | RX0EN | 1 | 下游数据只从 DRX0 进总线（一根脚，TDM16） |
| 3 | TXBCLKINV | 0 | DTX0/DTX1 在 BCLK **上升沿**变化 |
| 2 | TX2PINTL | 0 | 两根发送脚不交错。低 16 时隙在 DTX0，高 16 时隙在 DTX1 |
| 1 | TX1EN | 1 | 打开 DTX1 |
| 0 | TX0EN | 1 | 打开 DTX0。两根脚合起来就是软件缓冲里的 32 时隙 |

方向按 A2B 的脚命名，不按 SHARC 的 TX/RX 命名。`TX0EN`+`TX1EN` 是收发器的 DTX0/DTX1，把上游数据送给 SHARC，对应 SRU 上的 `SPT1_BD0` / `SPT1_BD1`（两根 TDM16，软件 RX 缓冲 32 时隙）。`RX0EN` 只有一根，DRX0 接收下游；`RX1EN` 关闭。SHARC 的 `SPT1_AD0` / `SPT1_AD1` 仍按两根脚在发，脚本只让 A2B 接收其中接在 DRX0 上的那一根。`TX2PINTL = 0` 时，两根发送脚不交错：低半时隙在 DTX0，高半在 DTX1。

**`SLOTFMT (0x10) = 0x42`**：`UPFMT=0`（上游不压缩、不 ECC），`UPSIZE=4`（上游 24 bit），`DNFMT=0`，`DNSIZE=2`（下游 16 bit）。不满 32 bit 的时隙在 I2S 口上 MSB 对齐，低位补 0。

**`DATCTL (0x11) = 0x03`**：bit1 `UPS` 与 bit0 `DNS` 都打开，上游和下游数据时隙使能。bit7 `STANDBY` 为 0。写主节点时芯片会广播到已发现的从节点。

**`SWCTL (0x09)`**：发现流程里写 `0x01`（只置 `ENSW`，打开下游电源开关）和 `0x21`（`ENSW` 加上 MODE 所在的 bit5）。bit 名：`DISNXT, MODE, DIAGMODE, ENSW`。

**`I2SRRATE = 0x01`**：表末尾的降速率寄存器，按导出值写入，本笔记不把它解释成另一套采样率。主时钟仍是 DAC 的 48 kHz。

`INTMSK0 = 0x70`，`INTMSK2 = 0x0D`，`BECCTL = 0xFF`，`RESPCYCS = 0x46`（与第一次发现的响应周期相同）。监视函数 `Ad2428Mon` 另读 `INTSRC`、`INTTYPE`、`BECNT (0x1F)`，然后把 `BECNT` 写 0 清掉。读 `INTSRC` 再读 `INTTYPE` 会清除该中断。误码类型 0–6（`HDCNTERR` … `SRFCRCERR`）计入 `data_err_cnt`。

#### USBi：先有时钟，再独占 I2C

USBi 是插在 TWI2 上的 I2C 主机。它不产生 BCLK，也不产生 FS。A2B 主节点要锁定 PLL，必须先看到 DAC 送出来的位时钟和帧时钟。USBi 一旦插上就占着 SCL/SDA，固件再 `Twi2Open` 会和它抢主机。

固件侧现在强制的顺序就是 `AudioFlowInit` 的顺序，并在 `Ad24280Init` 里再查一次：

1. SPU、UART、TMR、switch（codec 退出复位并等待 ≥ 300 ms）、SRU。
2. `Adau19620Init` 把 DAC PLL 锁定，`Adau1962ClkReady()` 变为 1。这一步之后 BCLK/FS 才存在。
3. `Adau19790Init` 使用同一对时钟，并在返回前 `Twi2Stop`。
4. `Ad24280Init`：时钟未锁定则打日志返回；`Twi2IsIdle()` 为假则打日志返回。通过后才 `Twi2Open`、回放 829 条、`Twi2Stop`。
5. `SpInit` 最后启动 DMA。

用 USBi 在 SigmaStudio 里发现或下载时，人要做的是：

1. 先让上面的第 1–3 步跑完，确认 BCLK/FS 已经从 DAC 出来。调试时可以停在 A2B 初始化之前，但时钟必须已经锁定。
2. 固件必须已经 `Twi2Stop`。不要在 USBi 还插着的时候调用 `Ad24280Init` 或任何其它 `Twi2Open`。
3. 插上 USBi，做发现或导出。
4. **拔掉 USBi**，再让 `Ad24280Init` 成为 TWI2 上唯一的主机。

软件无法探测 USBi 是否还插着。`Twi2IsIdle` 只表示本核自己没有打开 TWI2。

---

<a id="english"></a>

## English

### §1 Architecture

Bare-metal audio on **ADSP-21593** (dual SHARC+), board **ADZS-21593-EZKIT / SOM-21593 + SOMCRR-EZKIT**. One A2B master (AD24280, I2C `0x68` / `0x69`). AD24281 is an empty stub and `Ad24281Init()` stays commented out.

CCES Core 1 (`sharc1/`) owns every driver. Core 2 (`sharc2/`) calls `adi_initComponents()` and spins. Boot in `sharc1.c`: `adi_initComponents()` (SEC, pinmux, MCAPI) → `adi_core_enable(ADI_CORE_SHARC1)` (the CCES template: this core enables the other SHARC) → `AudioFlowInit()` → `MonTmp()` forever. Audio runs from the SPORT group callback via `INTR_TRU0_INT4`, not from the main loop.

`AudioFlowInit` order: **SPU → UART → TMR → switch → SRU → ADAU1962 → ADAU1979 → AD24280 → SPORT**. SPU before any DMA master. UART RX DMA before the 1 ms timer that calls `Uart0Poll`. Switch releases codec reset and waits ≥ 300 ms. SRU fans the DAC pins out before the DAC PLL is programmed. The DAC is the only BCLK/FS master; the ADC, the A2B transceiver, and every half-SPORT are slaves. A2B discovery runs only after that PLL reports lock and TWI2 is idle. SPORT DMA starts last.

Pinmux claims only UART0 (PA6–PA9) and TWI2 (PA14/PA15). DAI audio is the SRU. There is no `adi_pwr_Init`. `timer.c` assumes **SCLK = 125 MHz**. The codec MCLK is the board **24.576 MHz `AUDIO_CLK`**.

`AudioProcess` copies ADC channels 0–3 to the DAC, fills DAC channels 4–7 with a 1 kHz sine, and plays A2B slots 0/2/1/3 on DAC channels 8–11. Buffers are channel-major, `ptr[ch * 48 + i]`.

### §2 Buses

No SPI driver. Names such as `~MicroSD_SPI` in `switch0_reg.c` are GPIO-expander pins, not a programmed SPI controller.

**UART0.** `adi_uart_ConfigBaudRate(h, 1, 136)`. At the 125 MHz SCLK used by the timer comment, 125e6/136 ≈ 919118 baud, 0.27% below the 921600 comment; that is the nearest integer. Word length, parity, and stop bits are left at the driver’s default (8-N-1). RTS/CTS pins are muxed and no hardware flow control is enabled. RX is a 512-byte cyclic DMA on **DMA21**; the write index is `512 - XCNT_CUR`, and a remaining count of 0 maps to index 0. TX is stop-mode DMA, at most 512 bytes, and a multiple of 512 does not append a zero-length descriptor. The live monitor bytes are `0xAA` (A2B), `0xBB` (UART counters), `0xCC` (relative CPU load). `TuneRx` / `TuneTx` implement a separate frame that `MonTmp` does not dispatch: header `5A A5` (not in the CRC), Modbus CRC16 from `0xFFFF` over cmd and payload, CRC low byte first. Command `0x01` has a one-byte length; `0x02`–`0x04` do not.

**TWI2.** Prescale 12 (SCLK toward a 10 MHz TWI timebase), 100 kHz, 50% duty, 7-bit addresses. Short write is one `adi_twi_Write` of `{reg, data}` with no repeated start. Short read is a one-byte write with repeated start, then a one-byte read. Long write is one buffer: MSB-first address of 1, 2, or 4 bytes plus up to 128 data bytes. `Twi2Open` refuses a second open; `Twi2Stop` is a no-op when idle. Slaves: switch `0x22`, DAC `0x04`, ADC `0x11`, A2B `0x68` / `0x69`.

**Switch.** MCP23017-style. `IODIRA 0x00 = 0x18`, `IODIRB 0x01 = 0x00`, `GPIOB 0x13 = 0xFD`. `GPIOA 0x12` is `0x05` while `~ADAU_RESET` (bit 5) stays low, then `0x25` to release it. GPIO is written before IODIR. After `Twi2Stop`, 300e6 `__builtin_emuclk` cycles cover the ADAU1962A 300 ms PU/RST spec at CCLK ≤ 1 GHz.

**SPORT.** 48 kHz, 32-bit slots, early frame sync, external BCLK/FS, multichannel mode. `ConfigClock` is `(h, 32, bUseIntlClk, bRisingEdge, bGatedClk)`. Internal clock and gated clock are false on all six half-SPORTs. SP1B’s rising-edge bit is the opposite of SP1A’s; that stagger stays. `ConfigMC` window 15 is 16 slots (DAC/ADC); window 31 is 32 slots (A2B). The 32-slot buffer is two physical TDM16 pins (`PRIM_SEC`), not one 32-slot wire. BCLK is 16 × 32 × 48 kHz = 24.576 MHz.

**SRU.** DAI1 PB05 (BCLK) and PB04 (FS) from the DAC feed SPT0/1/2 and the ADC/A2B clock pins. Data pins are listed in the Chinese section. **SPU** secure-enables SPORT0/1/2 (IDs 63–68), UART0 (29), UART0 RX DMA (79), UART0 TX DMA (80).

**TMR0.** Continuous PWM, period 125000 SCLK ticks = 1 ms at 125 MHz. `adi_tmr_Config` runs before enable. The only IRQ action is `Uart0Poll`.

**ADAU1962** at `0x04` is the clock master. PLL writes `PLL_CLK_CTRL0` `0x01` then `0x05` (512×fs, matching 24.576 MHz), then `PLL_CLK_CTRL1 = 0x22`. Lock is bit 2 of that register and is polled, not written. Final `DAC_CTRL0 = 0x20` is TDM16 and 48 kHz. `DAC_CTRL1 = 0x43` and `DAC_CTRL2 = 0x00` match the comments in `adau19620_reg.c` (MSB first, falling-edge latch, master, 32-bit slot, 256× OSR).

**ADAU1979** at `0x11` is the SAI slave. `PLL_CONTROL = 0x03` (MCS for 512×fs in the 32–48 kHz band; bit 7 is PLL_LOCK). `SAI_CTRL0 = 0x22`: SDATA_FMT I2S, SAI TDM16, FS 32–48 kHz. `SAI_CTRL1 = 0x08`: 32-bit slot, 24-bit data, pulse FS, MSB first, slave. `BLOCK_POWER_SAI` goes `0x70` then `0x7F`.

### §3 A2B script and USBi

`ad24280_cfg` has **829** `Ad2428Reg` rows from SigmaStudio: **765 WRITE, 49 READ, 15 DELAY**. Every row has `reg_width = 1`, `val_width = 1`, `val_len = 1`. Devices: `0x68` × 158, `0x69` × 656, `0x00` × 15 (delays only).

`Ad2428Cfg` writes one register byte, discards reads, and busy-waits `value × 0x100000` on DELAY. A two-byte delay, if a future export has one, is big-endian. Index 1 (the first delay, `0x19`) polls INTTYPE **255** `MSTR_RUNNING`. A DELAY whose previous register is `0x13` polls INTTYPE **24** `DSCDONE`. Twelve discovery bytes, decreasing: `46 42 3E 3A 35 31 2D 29 25 21 1D 19`.

Master `CONTROL (0x12)`: `0x84` MSTR|SOFTRST, later `0x81` MSTR|NEWSTRCT, finally `0x82` MSTR|ENDDSC. `I2SGCFG (0x41) = 0x44` is written before the first DISCVRY: early pulse FS, 32-bit slot, TDM16. `I2SCFG (0x42) = 0x93`: sample DRX on falling BCLK, RX0 only, drive DTX on rising BCLK, TX0 and TX1 both on, no interleave. `SLOTFMT = 0x42`: 24-bit upstream, 16-bit downstream, no compression. `DATCTL = 0x03`: UPS and DNS enabled. `SWCTL` is written as `0x01` (ENSW) and `0x21`.

USBi is an I2C master on TWI2 and does not generate BCLK or FS. Firmware order is the list in §1, checked again in `Ad24280Init`: DAC PLL must already be locked (`Adau1962ClkReady`) and this core must not hold TWI2 (`Twi2IsIdle`). For a SigmaStudio session, bring clocks up first, stop TWI, plug USBi, then unplug it before `Ad24280Init`. Software cannot see that USBi is still attached.
