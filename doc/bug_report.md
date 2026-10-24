# 缺陷复查 / Bug review

英文摘要：只记录能在源码或数据手册上对上的问题。已改的项写了函数名；没改的项写了原因。构建结果见文末。

复查范围是 `origin/feat/base_audio_framework` 上的 `sharc1/`、`sharc2/` 和已有 `doc/`。`ad24280_reg.c` 的 829 条命令已逐条统计：全部是 `reg_width = 1`、`val_width = 1`、`val_len = 1`。

## 已修复

### 1. TMR0 的周期没有写入硬件

`Tmr0Cfg()`（`sharc1/src/driver/timer/timer.c`）只给 `tmr0_cfg` 赋值。`Tmr0Init()` 原先在 `adi_tmr_Open()` 之后直接 `adi_tmr_Enable()`，没有 `adi_tmr_Config()`。`Period = 125000` 因此不会进入定时器。`Tmr0Callback()` 里的 `Uart0Poll()` 不会按 1 ms 跑，RX 写指针停在 0。

修复：`Open` → `Tmr0Cfg` → `adi_tmr_Config(tmr0_dev, &tmr0_cfg)` → `Enable`。CCES `services/tmr/adi_tmr.h` 的 config 参数是指针。Open 或 Config 失败时不 Enable，并在 Config 失败时 `adi_tmr_Close()`。

### 2. 定时器早于 UART DMA

`AudioFlowInit()` 原顺序是 `Spu0Init` → `Tmr0Init` → `Uart0Init`。定时器中断调用 `Uart0Poll()`，而 `Uart0Poll()` 读 `UART0_RXDMA_XCNT_CUR`。DMA 还没启动时这个计数没有有效的描述符。

修复：`Spu0Init` → `Uart0Init` 成功 → `Tmr0Init`。`Uart0Init()` 现在返回 0/1；Open、波特率、回调或 `DMARead` 失败则关闭句柄且不启动定时器。`Uart0Poll()` / `Uart0Wr()` 在 `uart0_ready == 0` 时直接返回。

### 3. UART RX 覆盖判断有一条恒假

`Uart0Poll()` 第三条是：

```c
cur_wr_idx >= old_wr && cur_wr_idx < old_wr
```

两个比较不能同时成立，写指针回绕且读指针落在低半区时检测不到覆盖。另外 `XCNT == 0` 时 `DMA_LEN - 0 == 512`，下标越出 0..511，随后的读指针比较会把同一圈数据再消费一遍。

修复：`UartRxLapped()`。本拍写入字节数大于等于「从旧写指针正向走到读指针」的距离则记一次 overrun。空队列（`rd == wr`）只有写满整整 512 字节才记。`XCNT == 0` 归一成下标 0；`XCNT > 512` 丢弃本拍。

### 4. `Uart0Rd()` 把空读记成 underrun

`main` → `MonTmp()` 在没有字节时每次调用都 `underrun_cnt++`。这是空队列，不是 DMA 覆盖。计数会以主循环速率上涨，`Uart0Mon()` 无法用来判断丢数。

修复：空读仍返回 1，不再加计数。覆盖只由 `Uart0Poll()` 累加 `overrun_cnt`。`underrun_cnt` 字段保留，正常轮询下保持 0。

### 5. `Uart0Wr()` 的超时和 0 长度

超时计数在函数入口取一次 `TIME_BND`，长度大于 512 时后面的分块接着用剩下的数，长日志会提前超时。`len == 0` 仍会把 `XCount` 设成 0 并启动 TX DMA，完成回调可能不来，`is_tx_over` 一直为 0。`DMAWrite` 失败时同样不会有回调。

修复：每块单独计时；`len == 0` 直接成功返回；`DMAWrite` 失败时把 `is_tx_over` 置回 1 并返回 1。

### 6. `ShtLog()` 缺少头文件，截断判断差 1

`sharc1/src/algo/log/log.c` 使用 `va_list` 和 `vsnprintf`，原文件只包含 `uart/uart.h`。`vsnprintf` 的返回值是「若缓冲区足够会写出的字符数」。`len > 128` 在返回值等于 128 时仍把 128 个字节交给 `Uart0Wr()`，缓冲区里有效的是 127 个字符加一个 `'\0'`。截断时原来返回 0，而 `Uart0Wr` 的 0 表示成功。

调用点 `ad2428.c`、`adau1962.c`、`adau1979.c`、`sport.c` 都没包含 `log/log.h`。`ShtLog` 是变参函数，没有原型的调用按 SHARC 调用约定是未定义行为。

修复：补上 `<stdarg.h>`、`<stdio.h>`，调用点包含 `log/log.h`。返回值 `< 0` 或 `>= sizeof(msg)` 时返回 1。

### 7. `tune.c` 不能按 C 编译，帧格式和 CRC 对不上

`TuneTx()` 把 `(uint8_t *)(&len) + i` 传给 `Crc16Update()`。第二个参数类型是 `uint8_t`，这个表达式是指针，没有解引用。`Uart0Wr(&len, 4)` 的指针类型也和 `uint8_t *` 不一致。`memset` 没有 `<string.h>`。

接收端 `TuneRx()` 的长度是 1 字节，且只有带 payload 的命令才有长度字段。发送端固定写 4 字节长度。文件头注释是：

| 命令 | 含义 |
|------|------|
| 0x00 | 回写测试（应带 payload） |
| 0x01 | A2B monitor |
| 0x02 | UART monitor |
| 0x03 | CPU 负载 |
| 0x05 / 0x06 | ACK / NACK |

`CmdProc` / `CrcProc` 却从 0x01 开始，和注释以及 `ack[]` 里的命令字节 0x05 错一格。用本文件的 CRC 表计算，命令 0x05 的 Modbus CRC 是 `0x437F`，数组里写的是 `0x6F 0x43`；0x06 应为 `0x3F 0x42`，数组里是 `0xFF 0x76`。没有任何单字节能算出原来的 ACK CRC。

修复：`TuneTx` 对 0x00 发送 1 字节长度和 payload，其它命令只有 `头 + cmd + CRC16`（低字节在前）。解析器和注释对齐。ACK/NACK 常量改为正确 CRC，并在 `TuneRx` 校验结束后发出。`payload_idx` 改为 `uint16_t`，避免和 `MAX_PAYLOAD_LEN`（512）比较时被 8 位宽度截断。`tune.h` 补上声明。

`TuneRx` 仍没有接到 `main`。`TuneProc()` 里的标志位处理仍是空的，这是 `todo.md` 里的未完成功能，不是这次能补完的协议实现。

### 8. DAC 未锁相时仍跑 A2B 和 SPORT

`Adau1962Pll()` 超时只打印，`Adau19620Init()` 照样返回。`AudioFlowInit()` 继续 `Ad24280Init()` 和 `SpInit()`。AD2428 的发现和 SPORT 从机都依赖 ADAU1962 经 SRU 送出的 BCLK/FS。PLL 未锁时总线上没有位时钟，发现会失败，SPORT DMA 对着空时钟跑。

`PLL_CLK_CTRL1`（0x01）的 bit 2 是只读 `PLL_LOCK`（ADAU1962A 数据手册 Table 26）。代码里的 `(val & 0x04)` 是对的，没有改掩码。

修复：`Adau19620Init()` 返回 0/1。失败时不写后面的 DAC 配置表，并 `Twi2Stop()`。`AudioFlowInit()` 用返回值调用 `Ad2428ClkReady()`。时钟未就绪时 `Ad24280Init()` 不打开 TWI2，`SpInit()` 不调用。

### 9. USBi 与片上 I2C 主机抢总线

USBi 只转接 TWI2，不产生 BCLK/FS，插入后一直占着总线。原 `Ad24280Init()` 无条件 `Twi2Open()`。

修复：

- `Ad2428ClkReady(0)` 时 `Ad24280Init()` 返回，不 `Twi2Open()`。
- 编译宏 `AD2428_USBI_OWNS_TWI`（`ad2428.h`，默认 0）。置 1 时，时钟就绪后打印并返回，TWI2 保持关闭，给 SigmaStudio 用。
- `Twi2Open()` 在会话已打开时失败返回，避免两个调用者叠同一个 handle。`Twi2Stop()` 在未打开时不 `Close`。
- Master 运行超时或某个节点 `DSCDONE` 超时，`Ad2428Cfg()` 返回 1，`Ad24280Init()` 立刻 `Twi2Stop()`。不再把剩余命令堆在一条已经失败的总线上。

这张表是 12 个从节点。发现超时就停止，是因为后面的 `DISCVRY` 响应周期和最后的 `CONTROL = 0x82` 都假设前面的节点已经在网上。节点数不是 12 时要换脚本，而不是靠超时后继续写。

### 10. `Twi2DevRegLngWr()` 把一次寄存器写拆成两次 I2C 事务

函数把地址字节以 `bRepeatStart = true` 写出，再把 payload 用第二次 `adi_twi_Write()` 写出。Repeated START 会重新发送从机地址。从机看到的是两次寻址写，不是「寄存器地址后面紧跟数据」的一次写。

当前 829 条脚本都走 `Twi2DevRegShtWr()`，不会调用长写。函数本身的帧是错的，后面做外设长写会踩上。

修复：地址按大端放在 payload 前面，一次 `adi_twi_Write(..., false)`。超过 `4 + 256` 字节则直接返回。

## 核对过、没有改

| 项 | 结论 |
|----|------|
| `adi_core_enable(ADI_CORE_SHARC1)` | 与 CCES Core1 模板一致：本工程是启动核，这个调用拉起第二个 SHARC。文件头注释写的就是 enable core 2。 |
| SPORT `ConfigClock` 第 4 个参数在 SP1A/SP1B 上不同 | `sport.md` 写明是 A2B 回程的相位错开，不是笔误。六路的第 3 个参数都是 `false`（外部时钟，DAC 做主机）。 |
| `AD24281` 空表、`Ad24281Init` 被注释 | 官方板只有一路 A2B。空桩是故意的。 |
| `cpu_load_ = emuclk差 / 1000` | `sport.md` 写的就是这个比例，不是百分比。 |
| ADAU1979 PLL 锁相看 bit 7 | 与 ADAU1979 `PLL_CONTROL` 的 lock 位一致。 |
| ADAU1962 PLL 锁相看 bit 2 | 与数据手册一致。 |
| `i == 1` 等待 `MSTR_RUNNING` | 脚本第 0 条是 `CONTROL = 0x84`，第 1 条是 25 ms 延时。后面的 `CONTROL = 0x81` 是 ENDDSC，不应再等 0xFF。条件收紧为「下标 1 且上一条寄存器是 0x12」。 |
| DELAY 的 `val_len == 2` 只取 `val_ptr[1]` | 本表 15 条延时全是 1 字节。2 字节分支没有样本，不猜端序。 |
| 脚本 READ 不比较 `val_ptr` | INTTYPE 占位字节是 `0x00`，不是 `0xFF` 或 `24`。拿它做期望值会把成功的发现判失败。有效判断在 DELAY 之后的轮询里。 |
| SPU ID 63–68、29、79–80 | 与本仓库 `doc/spu.md` 一致。HRM 里的 SEC 中断号是另一套编号，不能拿来改这些 ID。 |
| `UART0_RXDMA_XCNT_CUR` | 仓库里没有定义，依赖 CCES 器件头。本次没有改成猜出来的 DMA 通道地址。若编译器报未声明，应改读 UART RX 目标 PDMA 的 `CurrXCount`，不要另造一个地址。 |
| `sharc1/.cproject` 里的 loader 路径 `D:\cces\SHARC\ldr\_prom.dxe` | 只影响 loader，不影响 dxe 链接。不改作者机器上的路径。 |
| `LngLog()`、`TuneProc()` 的业务、`Mon` 与 `Tune` 合并 | `todo.md` 里的未完成功能。只修了会编不过或帧格式错误的部分。 |

## 构建

云端虚拟机没有 CrossCore Embedded Studio，也没有 `cc21k`。仓库没有 Makefile / CMakeLists，不能在这里链出 dxe。

能跑的检查：

```text
gcc -std=c11 -Wall -Wextra -c \
  -I sharc1/src/algo -I sharc1/src/driver \
  sharc1/src/algo/log/log.c \
  sharc1/src/algo/tune/tune.c \
  sharc1/src/driver/audio_flow/audio_flow.c \
  sharc1/src/driver/switch/switch.c \
  sharc1/src/driver/adau1962/adau1962.c \
  sharc1/src/driver/adau1979/adau1979.c \
  sharc1/src/driver/ad2428/ad2428.c \
  以及各 *_reg.c
```

结果：退出码 0。`asm("nop;")` 在主机 gcc 上报隐式声明，那是 SHARC 编译器的关键字，不是这次引入的错误。

本地完整构建见 [engineering_notes.md](engineering_notes.md) 第 1 节末尾。
