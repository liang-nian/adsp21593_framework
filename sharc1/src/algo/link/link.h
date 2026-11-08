/*
 * Host frames. Header 5A A5, command, a 4-byte little-endian length and payload when
 * the length is greater than 0, then Modbus CRC16.
 * LinkRx dispatches a frame after it checks out. 0x01 is writeback. 0x02, 0x03, 0x04,
 * and 0x0B are monitor requests with no length field. 0x0C is a device log.
 * 0x05 through 0x0A are reserved and unused.
 */

#ifndef ALGO_LINK_LINK_H_
#define ALGO_LINK_LINK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_CMD_WB            (0x01u)
#define LINK_CMD_A2B_MON       (0x02u)
#define LINK_CMD_UART_MON      (0x03u)
#define LINK_CMD_CPU_MON       (0x04u)
#define LINK_CMD_CORE2_CPU_MON (0x0Bu)
#define LINK_CMD_LOG           (0x0Cu)

void LinkRx(void);
uint8_t LinkTxStart(uint8_t cmd, uint32_t len, uint16_t* crc);
uint8_t LinkTxData(uint8_t* data, uint32_t data_len, uint32_t* total_len,
                   uint16_t* crc);
uint8_t LinkTxEnd(uint32_t total_len, uint16_t crc);
uint8_t LinkTx(uint8_t cmd, uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
