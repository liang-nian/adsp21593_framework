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
  uint32_t cur_wr_idx = UART0_RX_DMA_CUR_XCNT;
  uint32_t rd_idx = uart0_msg.rx_rd_idx;
  uint8_t is_overrun = 0u;
  cur_wr_idx = DMA_LEN - cur_wr_idx;
  is_overrun |= (uart0_msg.rx_wr_idx < rd_idx &&
                cur_wr_idx > rd_idx);
  is_overrun |= (uart0_msg.rx_wr_idx < rd_idx &&
                cur_wr_idx < uart0_msg.rx_wr_idx);
  is_overrun |= (uart0_msg.rx_wr_idx >= rd_idx &&
                cur_wr_idx >= uart0_msg.rx_wr_idx &&
                cur_wr_idx < uart0_msg.rx_wr_idx);
  uart0_msg.overrun_cnt += is_overrun;
  uart0_msg.rx_wr_idx =  cur_wr_idx;
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
  uint32_t timer = TIME_BND;
  while (len > DMA_LEN) {
    while (!uart0_msg.is_tx_over) {
      timer--;
      if (!timer) {
        return 1u;
      }
    }
    uart0_msg.is_tx_over = 0u;
    memcpy(uart0_tx_buf, val, DMA_LEN);
    uart0_tx_desc.XCount = DMA_LEN;
    adi_uart_DMAWrite(uart0_dev, &uart0_tx_desc, 1, ADI_PDMA_STOP_MODE);
    len -= DMA_LEN;
    val += DMA_LEN;
  }
  while (!uart0_msg.is_tx_over) {
    timer--;
    if (!timer) {
      return 1u;
    }
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
