#include <services/spu/adi_spu.h>
#include <stdint.h>
#include <stdio.h>

#include "spu.h"

#define SPU0_DEV            (0u)
#define SPU0_SP0A           (63u)
#define SPU0_SP0B           (64u)
#define SPU0_SP1A           (65u)
#define SPU0_SP1B           (66u)
#define SPU0_SP2A           (67u)
#define SPU0_SP2B           (68u)
#define SPU0_UART0          (29)
#define SPU0_UART0_RX_DMA   (79)
#define SPU0_UART0_TX_DMA   (80)
#define SPU0_SVC_NUM        (9u)

#pragma default_section(ALLDATA, "seg_l1_block1")
static ADI_SPU_HANDLE spu0_dev;
static uint8_t spu0_mem[ADI_SPU_MEMORY_SIZE];
static uint32_t spu0_target[SPU0_SVC_NUM] = {
    SPU0_SP0A, SPU0_SP0B,
    SPU0_SP1A, SPU0_SP1B,
    SPU0_SP2A, SPU0_SP2B,
    SPU0_UART0, SPU0_UART0_RX_DMA, SPU0_UART0_TX_DMA
};
#pragma default_section(ALLDATA)

void Spu0Init(void) {
  uint32_t i;

  adi_spu_Init(SPU0_DEV, spu0_mem, NULL, NULL, &spu0_dev);

  for (i = 0; i < SPU0_SVC_NUM; ++i) {
    adi_spu_EnableMasterSecure(spu0_dev, spu0_target[i], true);
  }
};
