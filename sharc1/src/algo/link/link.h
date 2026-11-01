/*
 * UART frame layer used by the host port.
 * LinkRx drains UART and calls the matching module after a frame checks out.
 * LinkTxStart, LinkTxData and LinkTxEnd write one frame in three steps.
 * LinkTx is those three calls. A 0x01 payload is echoed only while wb_flag
 * is set, and the echo is a frame built by the same three calls.
 */

#ifndef ALGO_LINK_LINK_H_
#define ALGO_LINK_LINK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0x02–0x04 from the host have no length field.
 * 0x01 is writeback. wb_flag is set for that command. kPayload echoes
 * only while the flag is set, as a framed reply: header, then payload
 * bytes, then CRC once the remaining length is 0.
 * Device logs and monitor replies carry a 4-byte little-endian length
 * when len > 0. */
#define LINK_CMD_WB       (0x01u)
#define LINK_CMD_A2B_MON  (0x02u)
#define LINK_CMD_UART_MON (0x03u)
#define LINK_CMD_CPU_MON  (0x04u)
#define LINK_CMD_LOG      (0x0Cu)

void LinkRx(void);
uint8_t LinkTxStart(uint8_t cmd, uint32_t len, uint16_t* crc);
uint8_t LinkTxData(uint8_t* data, uint32_t data_len, uint32_t* total_len,
                   uint16_t* crc);
uint8_t LinkTxEnd(uint32_t total_len, uint16_t crc);
uint8_t LinkTx(uint8_t cmd, uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* ALGO_LINK_LINK_H_ */
