/*
 * tune.h ? reserved for future tuning / parameter tables.
 */

#ifndef ALGO_TUNE_TUNE_H_
#define ALGO_TUNE_TUNE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host requests 0x01–0x04 have no length byte.
 * Device logs and monitor replies carry a 1-byte length when len > 0.
 * 0x01 turns writeback on. The main loop then echoes UART bytes and
 * leaves TuneRx until the same frame is seen again. */
#define TUNE_CMD_WB       (0x01u)
#define TUNE_CMD_A2B_MON  (0x02u)
#define TUNE_CMD_UART_MON (0x03u)
#define TUNE_CMD_CPU_MON  (0x04u)
#define TUNE_CMD_LOG      (0x0Cu)

void TuneRx(uint8_t byte);
uint8_t TuneTx(uint8_t cmd, uint8_t* data, uint32_t len);

uint8_t TuneA2bMonFlag(void);
void TuneA2bMonFlagClr(void);
uint8_t TuneUartMonFlag(void);
void TuneUartMonFlagClr(void);
uint8_t TuneCpuMonFlag(void);
void TuneCpuMonFlagClr(void);
uint8_t TuneWbFlag(void);
void TuneWbFlagClr(void);

#ifdef __cplusplus
}
#endif

#endif /* ALGO_TUNE_TUNE_H_ */
