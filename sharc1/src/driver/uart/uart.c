#include <drivers/uart/adi_uart.h>
#include <sys/platform.h>
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
/* ADSP-21593 / SC594: UART0 TX is DMA20, UART0 RX is DMA21.
 * XCNT_CUR is the transfers still remaining in the active descriptor. */
#define UART0_RX_DMA_CUR_XCNT   (*pREG_DMA21_XCNT_CUR)

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

void Uart0Poll(void) {
  uint32_t xcnt = UART0_RX_DMA_CUR_XCNT;
  uint32_t cur_wr_idx;
  uint32_t old_wr = uart0_msg.rx_wr_idx;
  uint32_t rd_idx = uart0_msg.rx_rd_idx;
  uint8_t is_overrun = 0u;

  /* XCNT counts down and can read 0 for one sample at the wrap.
   * DMA_LEN - 0 would be an index past the buffer. */
  if (xcnt > DMA_LEN) {
    xcnt = DMA_LEN;
  }
  cur_wr_idx = DMA_LEN - xcnt;
  if (cur_wr_idx == DMA_LEN) {
    cur_wr_idx = 0u;
  }

  if (cur_wr_idx != old_wr) {
    if (old_wr < rd_idx) {
      /* Writer was behind the reader. Passing rd, wrapping through it,
       * or landing on it (full buffer, which aliases the empty check)
       * overwrites or drops unread bytes. */
      if (cur_wr_idx > rd_idx || cur_wr_idx < old_wr ||
          cur_wr_idx == rd_idx) {
        is_overrun = 1u;
      }
    } else if (cur_wr_idx < old_wr && cur_wr_idx > rd_idx) {
      /* Reader was at or behind the writer, and the write pointer wrapped
       * past rd. The previous condition (cur >= old && cur < old) was
       * unreachable, so this case never incremented overrun_cnt. */
      is_overrun = 1u;
    } else if (cur_wr_idx == rd_idx) {
      /* Writer landed on the reader. rd == wr is also the empty condition,
       * so the bytes just written would be dropped. */
      is_overrun = 1u;
    }
  }
  uart0_msg.overrun_cnt += is_overrun;
  uart0_msg.rx_wr_idx = cur_wr_idx;
};

static uint8_t Uart0WaitTx(void) {
  uint32_t timer = TIME_BND;

  while (!uart0_msg.is_tx_over) {
    timer--;
    if (!timer) {
      return 1u;
    }
  }
  return 0u;
};

uint8_t Uart0Rd(uint8_t* val) {
  uint32_t wr_idx = uart0_msg.rx_wr_idx;
  if (uart0_msg.rx_rd_idx == wr_idx) {
    uart0_msg.underrun_cnt += 1u;
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
  if (val == NULL || len == 0u) {
    return 0u;
  }
  while (len > DMA_LEN) {
    if (Uart0WaitTx() != 0u) {
      return 1u;
    }
    uart0_msg.is_tx_over = 0u;
    memcpy(uart0_tx_buf, val, DMA_LEN);
    uart0_tx_desc.XCount = DMA_LEN;
    adi_uart_DMAWrite(uart0_dev, &uart0_tx_desc, 1, ADI_PDMA_STOP_MODE);
    len -= DMA_LEN;
    val += DMA_LEN;
  }
  /* len is a multiple of DMA_LEN: the loop already queued every byte.
   * A follow-up DMA with XCount 0 never raises TX_PROCESSED, so
   * is_tx_over stays 0 and every later write times out. */
  if (len == 0u) {
    return 0u;
  }
  if (Uart0WaitTx() != 0u) {
    return 1u;
  }
  uart0_msg.is_tx_over = 0u;
  memcpy(uart0_tx_buf, val, len);
  uart0_tx_desc.XCount = len;
  adi_uart_DMAWrite(uart0_dev, &uart0_tx_desc, 1, ADI_PDMA_STOP_MODE);
  return 0u;
};

void Uart0Init(void) {
    adi_uart_Open(UART0_DEV, ADI_UART_DIR_BIDIRECTION, uart0_mem,
                  ADI_UART_BIDIR_MEMORY_SIZE, &uart0_dev);
    adi_uart_ConfigBaudRate(uart0_dev, UART0_PRESCALE, UART0_DIV);
    adi_uart_RegisterCallback(uart0_dev, Uart0Callback, NULL);
    PrepareDesc();
    adi_uart_DMARead(uart0_dev, &uart0_rx_desc, 1u, ADI_PDMA_DESCRIPTOR_LIST);
};

void Uart0Close(void) {
  adi_uart_Close(uart0_dev);
};

void Uart0Mon(void) {
  char msg[32];
  uint32_t len;

  len = snprintf(msg, sizeof(msg), "underrun cnt: %d.\n",
        uart0_msg.underrun_cnt);
  Uart0Wr((uint8_t*)msg, len);
  len = snprintf(msg, sizeof(msg), "overrun cnt: %d.\n",
        uart0_msg.overrun_cnt);
  Uart0Wr((uint8_t*)msg, len);
};
