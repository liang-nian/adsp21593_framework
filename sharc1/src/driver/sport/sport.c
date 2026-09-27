#include <services/int/adi_int.h>
#include <services/int/adi_sec.h>
#include <drivers/sport/adi_sport.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

#include "sport.h"
#include "audio_flow/audio_flow.h"
#include "uart/uart.h"

#define SP0A_DEV        (0u) // to dac
#define SP0B_DEV        (0u) // from adc
#define SP1A_DEV        (1u) // to a2b0
#define SP1B_DEV        (1u) // from a2b0
#define SP2A_DEV        (2U) // to a2b1
#define SP2B_DEV        (2U) // from a2b1
#define DMA_DESC_NUM    (2u)
#define SP_DEV_NUM      (6u)
#define EMA_K           (0.001f)

typedef struct SpMsg {
  float cpu_load_;
  float max_cpu_load_;
  float ave_cpu_load_;
  float adc_in_[TDM_NORM * FRAME_LEN];
  float dac_out_[TDM_NORM * FRAME_LEN];
  float a2b0_in_[TDM_A2B * FRAME_LEN];
  float a2b0_out_[TDM_A2B * FRAME_LEN];
  float a2b1_in_[TDM_A2B * FRAME_LEN];
  float a2b1_out_[TDM_A2B * FRAME_LEN];
} SpMsg;

#pragma default_section(ALLDATA, "seg_l1_block1")
// 12 channels enable to dac.
ADI_CACHE_ALIGN static int32_t sp0a_buf0[FRAME_LEN * TDM_NORM];
ADI_CACHE_ALIGN static int32_t sp0a_buf1[FRAME_LEN * TDM_NORM];
// 4 channels enable from adc.
ADI_CACHE_ALIGN static int32_t sp0b_buf0[FRAME_LEN * TDM_NORM];
ADI_CACHE_ALIGN static int32_t sp0b_buf1[FRAME_LEN * TDM_NORM];
// 0 channels enable to a2b0.
ADI_CACHE_ALIGN static int32_t sp1a_buf0[FRAME_LEN * TDM_A2B];
ADI_CACHE_ALIGN static int32_t sp1a_buf1[FRAME_LEN * TDM_A2B];
// 24 channels enable from a2b0.
ADI_CACHE_ALIGN static int32_t sp1b_buf0[FRAME_LEN * TDM_A2B];
ADI_CACHE_ALIGN static int32_t sp1b_buf1[FRAME_LEN * TDM_A2B];
// 8 channels enable to a2b1.
ADI_CACHE_ALIGN static int32_t sp2a_buf0[FRAME_LEN * TDM_A2B];
ADI_CACHE_ALIGN static int32_t sp2a_buf1[FRAME_LEN * TDM_A2B];
// 24 channels enable from a2b1.
ADI_CACHE_ALIGN static int32_t sp2b_buf0[FRAME_LEN * TDM_A2B];
ADI_CACHE_ALIGN static int32_t sp2b_buf1[FRAME_LEN * TDM_A2B];

ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp0a_desc0;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp0a_desc1;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp0b_desc0;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp0b_desc1;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp1a_desc0;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp1a_desc1;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp1b_desc0;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp1b_desc1;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp2a_desc0;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp2a_desc1;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp2b_desc0;
ADI_CACHE_ALIGN static ADI_PDMA_DESC_LIST sp2b_desc1;

static uint8_t sp0a_mem[ADI_SPORT_MEMORY_SIZE];
static uint8_t sp0b_mem[ADI_SPORT_MEMORY_SIZE];
static uint8_t sp1a_mem[ADI_SPORT_MEMORY_SIZE];
static uint8_t sp1b_mem[ADI_SPORT_MEMORY_SIZE];
static uint8_t sp2a_mem[ADI_SPORT_MEMORY_SIZE];
static uint8_t sp2b_mem[ADI_SPORT_MEMORY_SIZE];

static ADI_SPORT_HANDLE sp0a_tx_dev;
static ADI_SPORT_HANDLE sp0b_rx_dev;
static ADI_SPORT_HANDLE sp1a_tx_dev;
static ADI_SPORT_HANDLE sp1b_rx_dev;
static ADI_SPORT_HANDLE sp2a_tx_dev;
static ADI_SPORT_HANDLE sp2b_rx_dev;
static ADI_GLOBAL_SPORT_HANDLE sp_group;
static ADI_SPORT_HANDLE group_dev[SP_DEV_NUM];
#pragma default_section(ALLDATA)

static SpMsg msg = {0};

float* adc_in(void) {
  return msg.adc_in_;
};

float* dac_out(void) {
  return msg.dac_out_;
};

float* a2b0_in(void) {
  return msg.a2b0_in_;
};

float* a2b0_out(void) {
  return msg.a2b0_out_;
};

float* a2b1_in(void) {
  return msg.a2b1_in_;
};

float* a2b1_out(void) {
  return msg.a2b1_out_;
};

void CpuMsgMon(void) {
  ShtLog("cpu load: %.3f.\n", msg.cpu_load_);
  ShtLog("max cpu load: %.3f.\n", msg.max_cpu_load_);
  ShtLog("ave cpu load: %.3f.\n", msg.ave_cpu_load_);
};

static void Buf0Process(void) {
  uint32_t i, j;

  for (i = 0; i < FRAME_LEN; ++i) {
    for (j = 0; j < TDM_NORM; ++j) {
      sp0a_buf0[i * TDM_NORM + j] =
          __builtin_conv_FtoR(msg.dac_out_[j * FRAME_LEN + i]);
      msg.adc_in_[j * FRAME_LEN + i] =
          __builtin_conv_RtoF(sp0b_buf0[i * TDM_NORM + j]);
    }
    for (j = 0; j < TDM_A2B; ++j) {
      sp1a_buf0[i * TDM_A2B + j] =
          __builtin_conv_FtoR(msg.a2b0_out_[j * FRAME_LEN + i]);
      sp2a_buf0[i * TDM_A2B + j] =
          __builtin_conv_FtoR(msg.a2b1_out_[j * FRAME_LEN + i]);
      msg.a2b0_in_[j * FRAME_LEN + i] =
          __builtin_conv_RtoF(sp1b_buf0[i * TDM_A2B + j]);
      msg.a2b1_in_[j * FRAME_LEN + i] =
          __builtin_conv_RtoF(sp2b_buf0[i * TDM_A2B + j]);
    }
  }
};

static void Buf1Process(void) {
  uint32_t i, j;

  for (i = 0; i < FRAME_LEN; ++i) {
    for (j = 0; j < TDM_NORM; ++j) {
      sp0a_buf1[i * TDM_NORM + j] =
          __builtin_conv_FtoR(msg.dac_out_[j * FRAME_LEN + i]);
      msg.adc_in_[j * FRAME_LEN + i] =
          __builtin_conv_RtoF(sp0b_buf1[i * TDM_NORM + j]);
    }
    for (j = 0; j < TDM_A2B; ++j) {
      sp1a_buf1[i * TDM_A2B + j] =
          __builtin_conv_FtoR(msg.a2b0_out_[j * FRAME_LEN + i]);
      sp2a_buf1[i * TDM_A2B + j] =
          __builtin_conv_FtoR(msg.a2b1_out_[j * FRAME_LEN + i]);
      msg.a2b0_in_[j * FRAME_LEN + i] =
          __builtin_conv_RtoF(sp1b_buf1[i * TDM_A2B + j]);
      msg.a2b1_in_[j * FRAME_LEN + i] =
          __builtin_conv_RtoF(sp2b_buf1[i * TDM_A2B + j]);
    }
  }
};

static void SpGroupCallback(void* app, uint32_t event, void* arg) {
  static uint8_t cnt = 0u;

  switch (event) {
    case ADI_SPORT_EVENT_BUFFER_PROCESSED:
      cnt++;
      if (cnt == 1u) {
        Buf0Process();
      }
      if (cnt == 2u) {
        Buf1Process();
        cnt = 0;
      }
      adi_sec_Raise((uint32_t)INTR_TRU0_INT4);
      break;
    default:
      break;
  }
};

static void AudioCallBack(uint32_t iid) {
  uint64_t cnt;

  cnt = __builtin_emuclk();
  AudioProcess();
  cnt = __builtin_emuclk() - cnt;
  // Cpu load = cnt / period cnt * totalt load.
  msg.cpu_load_ = cnt / 1000.f;
  msg.max_cpu_load_ = fmaxf(msg.cpu_load_, msg.max_cpu_load_);
  msg.ave_cpu_load_ = EMA_K * msg.cpu_load_ + (1 - EMA_K) * msg.ave_cpu_load_;
};

static void PrepareDesc(void){
  sp0a_desc0.pStartAddr = (int *)sp0a_buf0;
  sp0a_desc0.pNxtDscp = &sp0a_desc1;
  sp0a_desc0.Config = ENUM_DMA_CFG_XCNT_INT;
  sp0a_desc0.XCount = FRAME_LEN * TDM_NORM;
  sp0a_desc0.XModify = 4u;
  sp0a_desc0.YCount = 0u;
  sp0a_desc0.YModify = 0u;

  sp0a_desc1.pStartAddr = (int *)sp0a_buf1;
  sp0a_desc1.pNxtDscp = &sp0a_desc0;
  sp0a_desc1.Config = ENUM_DMA_CFG_XCNT_INT;
  sp0a_desc1.XCount = FRAME_LEN * TDM_NORM;
  sp0a_desc1.XModify = 4u;
  sp0a_desc1.YCount = 0u;
  sp0a_desc1.YModify = 0u;

  sp0b_desc0.pStartAddr = (int *)sp0b_buf0;
  sp0b_desc0.pNxtDscp = &sp0b_desc1;
  sp0b_desc0.Config = ENUM_DMA_CFG_XCNT_INT;
  sp0b_desc0.XCount = FRAME_LEN * TDM_NORM;
  sp0b_desc0.XModify = 4u;
  sp0b_desc0.YCount = 0u;
  sp0b_desc0.YModify = 0u;

  sp0b_desc1.pStartAddr = (int *)sp0b_buf1;
  sp0b_desc1.pNxtDscp = &sp0b_desc0;
  sp0b_desc1.Config = ENUM_DMA_CFG_XCNT_INT;
  sp0b_desc1.XCount = FRAME_LEN * TDM_NORM;
  sp0b_desc1.XModify = 4u;
  sp0b_desc1.YCount = 0u;
  sp0b_desc1.YModify = 0u;

  sp1a_desc0.pStartAddr = (int *)sp1a_buf0;
  sp1a_desc0.pNxtDscp = &sp1a_desc1;
  sp1a_desc0.Config = ENUM_DMA_CFG_XCNT_INT;
  sp1a_desc0.XCount = FRAME_LEN * TDM_A2B;
  sp1a_desc0.XModify = 4u;
  sp1a_desc0.YCount = 0u;
  sp1a_desc0.YModify = 0u;

  sp1a_desc1.pStartAddr = (int *)sp1a_buf1;
  sp1a_desc1.pNxtDscp = &sp1a_desc0;
  sp1a_desc1.Config = ENUM_DMA_CFG_XCNT_INT;
  sp1a_desc1.XCount = FRAME_LEN * TDM_A2B;
  sp1a_desc1.XModify = 4u;
  sp1a_desc1.YCount = 0u;
  sp1a_desc1.YModify = 0u;

  sp1b_desc0.pStartAddr = (int *)sp1b_buf0;
  sp1b_desc0.pNxtDscp = &sp1b_desc1;
  sp1b_desc0.Config = ENUM_DMA_CFG_XCNT_INT;
  sp1b_desc0.XCount = FRAME_LEN * TDM_A2B;
  sp1b_desc0.XModify = 4u;
  sp1b_desc0.YCount = 0u;
  sp1b_desc0.YModify = 0u;

  sp1b_desc1.pStartAddr = (int *)sp1b_buf1;
  sp1b_desc1.pNxtDscp = &sp1b_desc0;
  sp1b_desc1.Config = ENUM_DMA_CFG_XCNT_INT;
  sp1b_desc1.XCount = FRAME_LEN * TDM_A2B;
  sp1b_desc1.XModify = 4u;
  sp1b_desc1.YCount = 0u;
  sp1b_desc1.YModify = 0u;

  sp2a_desc0.pStartAddr = (int *)sp2a_buf0;
  sp2a_desc0.pNxtDscp = &sp2a_desc1;
  sp2a_desc0.Config = ENUM_DMA_CFG_XCNT_INT;
  sp2a_desc0.XCount = FRAME_LEN * TDM_A2B;
  sp2a_desc0.XModify = 4u;
  sp2a_desc0.YCount = 0u;
  sp2a_desc0.YModify = 0u;

  sp2a_desc1.pStartAddr = (int *)sp2a_buf1;
  sp2a_desc1.pNxtDscp = &sp2a_desc0;
  sp2a_desc1.Config = ENUM_DMA_CFG_XCNT_INT;
  sp2a_desc1.XCount = FRAME_LEN * TDM_A2B;
  sp2a_desc1.XModify = 4u;
  sp2a_desc1.YCount = 0u;
  sp2a_desc1.YModify = 0u;

  sp2b_desc0.pStartAddr = (int *)sp2b_buf0;
  sp2b_desc0.pNxtDscp = &sp2b_desc1;
  sp2b_desc0.Config = ENUM_DMA_CFG_XCNT_INT;
  sp2b_desc0.XCount = FRAME_LEN * TDM_A2B;
  sp2b_desc0.XModify = 4u;
  sp2b_desc0.YCount = 0u;
  sp2b_desc0.YModify = 0u;

  sp2b_desc1.pStartAddr = (int *)sp2b_buf1;
  sp2b_desc1.pNxtDscp = &sp2b_desc0;
  sp2b_desc1.Config = ENUM_DMA_CFG_XCNT_INT;
  sp2b_desc1.XCount = FRAME_LEN * TDM_A2B;
  sp2b_desc1.XModify = 4u;
  sp2b_desc1.YCount = 0u;
  sp2b_desc1.YModify = 0u;
};

void SpInit(void) {
  adi_sport_Open(SP0A_DEV, ADI_HALF_SPORT_A, ADI_SPORT_DIR_TX,
                 ADI_SPORT_MC_MODE, sp0a_mem, ADI_SPORT_MEMORY_SIZE,
                 &sp0a_tx_dev);
  adi_sport_Open(SP0B_DEV, ADI_HALF_SPORT_B, ADI_SPORT_DIR_RX,
                 ADI_SPORT_MC_MODE, sp0b_mem, ADI_SPORT_MEMORY_SIZE,
                 &sp0b_rx_dev);
  adi_sport_Open(SP1A_DEV, ADI_HALF_SPORT_A, ADI_SPORT_DIR_TX,
                 ADI_SPORT_MC_MODE, sp1a_mem, ADI_SPORT_MEMORY_SIZE,
                 &sp1a_tx_dev);
  adi_sport_Open(SP1B_DEV, ADI_HALF_SPORT_B, ADI_SPORT_DIR_RX,
                 ADI_SPORT_MC_MODE, sp1b_mem, ADI_SPORT_MEMORY_SIZE,
                 &sp1b_rx_dev);
  adi_sport_Open(SP2A_DEV, ADI_HALF_SPORT_A, ADI_SPORT_DIR_TX,
                 ADI_SPORT_MC_MODE, sp2a_mem, ADI_SPORT_MEMORY_SIZE,
                 &sp2a_tx_dev);
  adi_sport_Open(SP2B_DEV, ADI_HALF_SPORT_B, ADI_SPORT_DIR_RX,
                 ADI_SPORT_MC_MODE, sp2b_mem, ADI_SPORT_MEMORY_SIZE,
                 &sp2b_rx_dev);

  adi_sport_ConfigData(sp0a_tx_dev, ADI_SPORT_DTYPE_SIGN_FILL, 31, false, false,
                       false);
  adi_sport_ConfigClock(sp0a_tx_dev, 32, false, true, false);
  adi_sport_ConfigFrameSync(sp0a_tx_dev, 31, false, false, false, true, false,
                            true);
  adi_sport_ConfigMC(sp0a_tx_dev, 1u, 15u, 0u, true);
  adi_sport_SelectChannel(sp0a_tx_dev, 0u, 15u);

  adi_sport_ConfigData(sp0b_rx_dev, ADI_SPORT_DTYPE_SIGN_FILL, 31, false, false,
                       false);
  adi_sport_ConfigClock(sp0b_rx_dev, 32, false, false, false);
  adi_sport_ConfigFrameSync(sp0b_rx_dev, 31, false, false, false, true, false,
                            true);
  adi_sport_ConfigMC(sp0b_rx_dev, 1u, 15u, 0u, true);
  adi_sport_SelectChannel(sp0b_rx_dev, 0u, 15u);

  adi_sport_ConfigData(sp1a_tx_dev, ADI_SPORT_DTYPE_SIGN_FILL, 31, false, false,
                       false);
  adi_sport_ConfigClock(sp1a_tx_dev, 32, false, false, false);
  adi_sport_ConfigFrameSync(sp1a_tx_dev, 31, false, false, false, true, false,
                            true);
  adi_sport_ConfigMC(sp1a_tx_dev, 1u, 31u, 0u, true);
  adi_sport_SelectChannel(sp1a_tx_dev, 0u, 31u);

  adi_sport_ConfigData(sp1b_rx_dev, ADI_SPORT_DTYPE_SIGN_FILL, 31, false, false,
                       false);
  adi_sport_ConfigClock(sp1b_rx_dev, 32, false, true, false);
  adi_sport_ConfigFrameSync(sp1b_rx_dev, 31, false, false, false, true, false,
                            true);
  adi_sport_ConfigMC(sp1b_rx_dev, 1u, 31u, 0u, true);
  adi_sport_SelectChannel(sp1b_rx_dev, 0u, 31u);

  adi_sport_ConfigData(sp2a_tx_dev, ADI_SPORT_DTYPE_SIGN_FILL, 31, false, false,
                       false);
  adi_sport_ConfigClock(sp2a_tx_dev, 32, false, true, false);
  adi_sport_ConfigFrameSync(sp2a_tx_dev, 31, false, false, false, true, false,
                            true);
  adi_sport_ConfigMC(sp2a_tx_dev, 1u, 31u, 0u, true);
  adi_sport_SelectChannel(sp2a_tx_dev, 0u, 31u);

  adi_sport_ConfigData(sp2b_rx_dev, ADI_SPORT_DTYPE_SIGN_FILL, 31, false, false,
                       false);
  adi_sport_ConfigClock(sp2b_rx_dev, 32, false, true, false);
  adi_sport_ConfigFrameSync(sp2b_rx_dev, 31, false, false, false, true, false,
                            true);
  adi_sport_ConfigMC(sp2b_rx_dev, 1u, 31u, 0u, true);
  adi_sport_SelectChannel(sp2b_rx_dev, 0u, 31u);

  group_dev[0] = sp0a_tx_dev;
  group_dev[1] = sp0b_rx_dev;
  group_dev[2] = sp1a_tx_dev;
  group_dev[3] = sp1b_rx_dev;
  group_dev[4] = sp2a_tx_dev;
  group_dev[5] = sp2b_rx_dev;

  adi_sport_CreateGlobalGroup(group_dev, SP_DEV_NUM, &sp_group, true, true);

  adi_sport_GlobalRegisterCallback(sp_group, SpGroupCallback, NULL);
  adi_int_InstallHandler((uint32_t)INTR_TRU0_INT4,
                         (ADI_INT_HANDLER_PTR)AudioCallBack, NULL, true);

  PrepareDesc();

  adi_sport_DMATransfer(sp0a_tx_dev, &sp0a_desc0, DMA_DESC_NUM,
                        ADI_PDMA_DESCRIPTOR_LIST, ADI_SPORT_CHANNEL_PRIM);
  adi_sport_DMATransfer(sp0b_rx_dev, &sp0b_desc0, DMA_DESC_NUM,
                        ADI_PDMA_DESCRIPTOR_LIST, ADI_SPORT_CHANNEL_PRIM);
  adi_sport_DMATransfer(sp1a_tx_dev, &sp1a_desc0, DMA_DESC_NUM,
                        ADI_PDMA_DESCRIPTOR_LIST, ADI_SPORT_CHANNEL_PRIM_SEC);
  adi_sport_DMATransfer(sp1b_rx_dev, &sp1b_desc0, DMA_DESC_NUM,
                        ADI_PDMA_DESCRIPTOR_LIST, ADI_SPORT_CHANNEL_PRIM_SEC);
  adi_sport_DMATransfer(sp2a_tx_dev, &sp2a_desc0, DMA_DESC_NUM,
                        ADI_PDMA_DESCRIPTOR_LIST, ADI_SPORT_CHANNEL_PRIM_SEC);
  adi_sport_DMATransfer(sp2b_rx_dev, &sp2b_desc0, DMA_DESC_NUM,
                        ADI_PDMA_DESCRIPTOR_LIST, ADI_SPORT_CHANNEL_PRIM_SEC);

  adi_sport_GlobalEnable(false);
  adi_sport_Enable(sp0a_tx_dev, true);
  adi_sport_Enable(sp0b_rx_dev, true);
  adi_sport_Enable(sp1a_tx_dev, true);
  adi_sport_Enable(sp1b_rx_dev, true);
  adi_sport_Enable(sp2a_tx_dev, true);
  adi_sport_Enable(sp2b_rx_dev, true);
  adi_sport_GlobalEnable(true);
};
