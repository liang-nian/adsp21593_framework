#include <drivers/uart/adi_uart.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "uart.h"

// Buffer size depends on baud rate and polling interval.
#define DMA_LEN                 (512u)
#define UART0_DEV               (0u)
#define UART0_DIV               (136u) // 921600
#define UART0_PRESCALE          (1u)
#define TIME_BND                (0x1000000u)
#define UART0_RX_DMA_CUR_XCNT   (*(volatile uint32_t *)UART0_RXDMA_XCNT_CUR)

typedef struct UartDmaMsg {
  uint8_t is_tx_over;
  uint32_t rx_rd_idx;
  uint32_t rx_wr_idx;
  uint32_t underrun_cnt;
  uint32_t overrun_cnt;
} UartDmaMsg;

#pragma default_section(ALLDATA, "seg_l1_block1")
ADI_CACHE_ALIGN static uint8_t uart0_rx_buf[DMA_LEN];
ADI_CACHE_ALIGN static uint8_t uart0_tx_buf[DMA_LEN];

ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST  uart0_tx_desc;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST  uart0_rx_desc;

static uint8_t uart0_mem[ADI_UART_BIDIR_MEMORY_SIZE];

static ADI_UART_HANDLE uart0_dev;
static uint8_t uart0_ready;

ADI_CACHE_ALIGN volatile static UartDmaMsg uart0_msg = {
    .underrun_cnt = 0u,
    .overrun_cnt = 0u,
    .is_tx_over = 1u,
    .rx_rd_idx = 0u,
    .rx_wr_idx = 0u
};
#pragma default_section(ALLDATA)

static void PrepareDesc(void) {
  uart0_tx_desc.pNxtDscp = NULL;
  uart0_tx_desc.pStartAddr = uart0_tx_buf;
  uart0_tx_desc.Config = ENUM_DMA_CFG_XCNT_INT;
  uart0_tx_desc.XCount = 0u;
  uart0_tx_desc.XModify = 1u;
  uart0_tx_desc.YCount = 0u;
  uart0_tx_desc.YModify = 0u;

  uart0_rx_desc.pNxtDscp = &uart0_rx_desc;
  uart0_rx_desc.pStartAddr = uart0_rx_buf;
  uart0_rx_desc.Config = ENUM_DMA_CFG_XCNT_INT;
  uart0_rx_desc.XCount = DMA_LEN;
  uart0_rx_desc.XModify = 1u;
  uart0_rx_desc.YCount = 0u;
  uart0_rx_desc.YModify = 0u;
};

static void Uart0Callback(void* param, uint32_t event, void* arg) {
  if (event == (uint32_t)ADI_UART_EVENT_TX_PROCESSED) {
    uart0_msg.is_tx_over = 1u;
  }
  if(event == (uint32_t)ADI_UART_EVENT_RX_PROCESSED) {}
};

/* Writer index is the next DMA store. rd == wr means empty, so a lap that
 * lands back on rd cannot be told from idle and is counted as an overrun.
 */
static uint8_t UartRxLapped(uint32_t old_wr, uint32_t rd, uint32_t cur) {
  uint32_t wrote;
  uint32_t until_rd;

  if (old_wr >= DMA_LEN || rd >= DMA_LEN || cur >= DMA_LEN) {
    return 0u;
  }
  if (cur >= old_wr) {
    wrote = cur - old_wr;
  } else {
    wrote = (DMA_LEN - old_wr) + cur;
  }
  if (wrote == 0u) {
    return 0u;
  }
  if (rd == old_wr) {
    until_rd = DMA_LEN;
  } else if (rd > old_wr) {
    until_rd = rd - old_wr;
  } else {
    until_rd = (DMA_LEN - old_wr) + rd;
  }
  return (wrote >= until_rd) ? 1u : 0u;
};

void Uart0Poll(void) {
  uint32_t xcnt;
  uint32_t cur_wr_idx;
  uint32_t old_wr;
  uint32_t rd_idx;

  if (!uart0_ready) {
    return;
  }
  xcnt = UART0_RX_DMA_CUR_XCNT;
  /* XCNT is the remaining count. 0 is the reload point (index wrapped to 0).
   * A count above DMA_LEN is not a live descriptor.
   */
  if (xcnt > DMA_LEN) {
    return;
  }
  cur_wr_idx = (xcnt == 0u) ? 0u : (DMA_LEN - xcnt);
  old_wr = uart0_msg.rx_wr_idx;
  rd_idx = uart0_msg.rx_rd_idx;
  if (old_wr >= DMA_LEN) {
    old_wr = 0u;
  }
  uart0_msg.overrun_cnt += UartRxLapped(old_wr, rd_idx, cur_wr_idx);
  uart0_msg.rx_wr_idx = cur_wr_idx;
};

uint8_t Uart0Rd(uint8_t* val) {
  uint32_t wr_idx = uart0_msg.rx_wr_idx;
  if (val == NULL) {
    return 1u;
  }
  /* Empty is the normal poll result. MonTmp spins on it; do not count that
   * as a ring underrun. Overrun is the DMA lap detected in Uart0Poll.
   */
  if (uart0_msg.rx_rd_idx == wr_idx) {
    return 1u;
  }
  *val = uart0_rx_buf[uart0_msg.rx_rd_idx];
  uart0_msg.rx_rd_idx++;
  if (uart0_msg.rx_rd_idx == DMA_LEN) {
    uart0_msg.rx_rd_idx = 0u;
  }
  return 0u;
};

uint8_t Uart0Wr(uint8_t* val, uint32_t len) {
  if (!uart0_ready || val == NULL) {
    return 1u;
  }
  if (len == 0u) {
    return 0u;
  }
  while (len > 0u) {
    uint32_t timer = TIME_BND;
    uint32_t chunk = (len > DMA_LEN) ? DMA_LEN : len;
    while (!uart0_msg.is_tx_over) {
      if (timer-- == 0u) {
        return 1u;
      }
    }
    uart0_msg.is_tx_over = 0u;
    memcpy(uart0_tx_buf, val, chunk);
    uart0_tx_desc.XCount = chunk;
    if (adi_uart_DMAWrite(uart0_dev, &uart0_tx_desc, 1, ADI_PDMA_STOP_MODE) != 0) {
      uart0_msg.is_tx_over = 1u;
      return 1u;
    }
    len -= chunk;
    val += chunk;
  }
  return 0u;
};

uint8_t Uart0Init(void) {
  if (uart0_ready) {
    return 0u;
  }
  if (adi_uart_Open(UART0_DEV, ADI_UART_DIR_BIDIRECTION, uart0_mem,
                    ADI_UART_BIDIR_MEMORY_SIZE, &uart0_dev) != 0) {
    return 1u;
  }
  if (adi_uart_ConfigBaudRate(uart0_dev, UART0_PRESCALE, UART0_DIV) != 0 ||
      adi_uart_RegisterCallback(uart0_dev, Uart0Callback, NULL) != 0) {
    adi_uart_Close(uart0_dev);
    return 1u;
  }
  PrepareDesc();
  if (adi_uart_DMARead(uart0_dev, &uart0_rx_desc, 1u,
                       ADI_PDMA_DESCRIPTOR_LIST) != 0) {
    adi_uart_Close(uart0_dev);
    return 1u;
  }
  uart0_ready = 1u;
  return 0u;
};

void Uart0Close(void) {
  if (!uart0_ready) {
    return;
  }
  adi_uart_Close(uart0_dev);
  uart0_ready = 0u;
};

void Uart0Mon(void) {
  char msg[32];
  uint32_t len;

  len = snprintf(msg, sizeof(msg), "underrun cnt: %u.\n",
        uart0_msg.underrun_cnt);
  Uart0Wr((uint8_t*)msg, len);
  len = snprintf(msg, sizeof(msg), "overrun cnt: %u.\n",
        uart0_msg.overrun_cnt);
  Uart0Wr((uint8_t*)msg, len);
};
