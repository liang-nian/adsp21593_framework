# TWI2 — I2C Host, Error Isolation, Open/Stop per Driver

## English

**Summary:** TWI2 @ 100 kHz. **Open → transfer → Stop inside each driver init** so a NAK/timeout on one device cannot poison the next.

### 1. Code implementation

**Role:** Shared **TWI2** control bus for switch, codecs, and A2B. Each init owns a full open/configure/stop session.

**API:** `Twi2Open`/`Twi2Stop`, short read/write, `Twi2DevRegLngWr` for A2B long addresses.

**Pattern:** Open → all registers for this device → Stop. Used in switch, ADAU1962, ADAU1979, AD24280 inits.

Device addresses: `0x22`, `0x04`, `0x11`, `0x68`/`0x69`.

### 2. Driver configuration

| Macro | Value |
|-------|-------|
| `TWI2_BIT_RATE` | 100 kHz |
| `TWI2_PRESCALE` | 12 |
| Duty | 50% |

### 3. Pitfalls

**Error cascading:** After a failed transaction the controller can stick in an error state; without closing the handle, later inits on the **same** bus keep failing. **Per-driver Open/Stop** resets the session.

**Endianness:** Little-endian SHARC memory vs I2C **MSB-first multi-byte register addresses** — `Twi2DevRegLngWr` builds big-endian address bytes explicitly (`seq[0]=(reg>>24)&0xFF`, …). Wire bit order (LSB-first) ≠ memory endianness.

**USBi:** Holds TWI2 while plugged in — [ad2428.md](ad2428.md).

**Pitfalls:** Missing `Stop`; invalid `reg_len` on long write returns silently; 100 kHz is conservative.

**Sources:** `twi.c`, `twi.h`


---

## 中文

**摘要：** **TWI2** @ **100 kHz**，prescale **12**；codec **短写**，A2B **长写（寄存器地址 MSBF）**。每个外设驱动 **`Twi2Open` → 配置 → `Twi2Stop`**，不要共用一个长期打开的会话。

### 1. 代码实现

**音频链位置：** 所有 I2C 控制面（U6 switch、ADAU1962/1979、AD24280）共用 **TWI2**；各 `*Init` 在配置阶段独占总线会话。

**公开 API（`twi.h` / `twi.c`）：**

| API | 用途 |
|-----|------|
| `Twi2Open` / `Twi2Stop` | 打开/关闭主机 |
| `Twi2DevRegShtWr` / `Twi2DevRegShtRd` | 8-bit 地址 + 1 字节数据 |
| `Twi2DevRegLngWr` | A2B：1/2/4 字节 **MSBF** 寄存器地址 + payload |
| `Twi2DevAddrSet` | 设当前 7-bit 地址 |

**调用约定（每个驱动 init 内）：**

```text
Twi2Open();
… 本设备全部寄存器 …
Twi2Stop();
```

Switch / ADAU1962 / ADAU1979 / AD24280 **各自**成对 Open/Stop。

**设备地址：** `0x22` switch，`0x04` DAC，`0x11` ADC，`0x68`/`0x69` A2B0。

### 2. 驱动配置

| 宏 | 值 |
|----|-----|
| `TWI2_DEV` | 2 |
| `TWI2_BIT_RATE` | 100 kHz |
| `TWI2_DUTY_CYCLE` | 50% |
| `TWI2_PRESCALE` | 12 |

### 3. 踩坑记录

**TWI 失败扩散：** ADI TWI 在 NAK / 仲裁失败 / 超时后，控制器可能留在 busy、未发 STOP、错误锁存等异常态。若不 Close/再 Open，**后续无关设备也会连续失败**。

```mermaid
flowchart LR
  A[驱动A 某次写失败] --> B[TWI 控制器异常态]
  B --> C[驱动B 本应正常也失败]
  C --> D[驱动C 继续失败…]
```

**每驱动单独 Open/Stop** 用一点开关开销换 **故障隔离**（init 频率低，可接受）。

**LSBF / 端序：** SHARC 内存默认 **小端**。I2C 多字节寄存器地址须 **MSBF 上总线** — `Twi2DevRegLngWr` 把 `reg` 按大端拆进 `seq[]` 再 `adi_twi_Write`，不能按小端内存直接发送。单字节地址无此问题。线路 **LSB-first 比特序** 与内存端序不是同一概念。

**USBi 互斥：** USBi 占用 TWI2 时固件不得 Open — [ad2428.md](ad2428.md)。

**其它坑：** 忘记 `Stop` → 下一驱动状态错乱；`LngWr` 的 `reg_len` 非 1/2/4 → 直接 return（静默失败）；100 kHz 偏保守，提速需整链回归 A2B 长包。

**源文件：** `twi.c`, `twi.h`
