#include <drivers/twi/adi_twi.h>
#include <stddef.h>
#include <stdint.h>

#include "twi.h"
#include "log/log.h"

#define TWI2_DEV          (2u)
#define TWI2_BIT_RATE     (100u) // khz
#define TWI2_DUTY_CYCLE   (50u)
#define TWI2_PRESCALE     (12u) // sclk / 10 mhz

#define TWI2_LNG_MAX      (4u + 256u)

#pragma default_section(ALLDATA, "seg_l1_block1")
static ADI_TWI_HANDLE twi2_dev;
static uint8_t twi2_mem[ADI_TWI_MEMORY_SIZE];
static uint8_t twi2_open;
static uint8_t twi2_lng_buf[TWI2_LNG_MAX];
#pragma default_section(ALLDATA)

uint8_t Twi2Open(void) {
  if (twi2_open) {
    ShtLog("twi2 already open.\n");
    return 1u;
  }
  if (adi_twi_Open(TWI2_DEV, ADI_TWI_MASTER, twi2_mem, ADI_TWI_MEMORY_SIZE,
                   &twi2_dev) != 0) {
    ShtLog("twi2 open fail.\n");
    return 1u;
  }
  if (adi_twi_SetPrescale(twi2_dev, TWI2_PRESCALE) != 0 ||
      adi_twi_SetBitRate(twi2_dev, TWI2_BIT_RATE) != 0 ||
      adi_twi_SetDutyCycle(twi2_dev, TWI2_DUTY_CYCLE) != 0) {
    adi_twi_Close(twi2_dev);
    ShtLog("twi2 cfg fail.\n");
    return 1u;
  }
  twi2_open = 1u;
  return 0u;
};

void Twi2DevAddrSet(uint8_t dev) {
  if (!twi2_open) {
    return;
  }
  adi_twi_SetHardwareAddress(twi2_dev, dev);
};

void Twi2DevRegShtWr(uint8_t dev, uint8_t reg, uint8_t val) {
  uint8_t wr_val[2u];

  if (!twi2_open) {
    return;
  }
  adi_twi_SetHardwareAddress(twi2_dev, dev);
	wr_val[0] = reg;
	wr_val[1] = val;
	adi_twi_Write(twi2_dev, wr_val, 2u, false);
};

uint8_t Twi2DevRegShtRd(uint8_t dev, uint8_t reg) {
	uint8_t read_val = 0u;

	if (!twi2_open) {
	  return 0u;
	}
	adi_twi_SetHardwareAddress(twi2_dev, dev);
	adi_twi_Write(twi2_dev, &reg, 1u, true);
	adi_twi_Read(twi2_dev, &read_val, 1u, false);
	return read_val;
};

void Twi2RegShtWr(uint8_t reg, uint8_t val) {
	uint8_t wr_val[2];

	if (!twi2_open) {
	  return;
	}
	wr_val[0] = reg;
	wr_val[1] = val;
	adi_twi_Write(twi2_dev, wr_val, 2u, false);
};

uint8_t Twi2RegShtRd(uint8_t reg) {
	uint8_t read_val = 0u;

	if (!twi2_open) {
	  return 0u;
	}
	adi_twi_Write(twi2_dev, &reg, 1u, true);
	adi_twi_Read(twi2_dev, &read_val, 1u, false);
	return read_val;
};

/* One I2C write: MSBF register address bytes followed by the payload, then STOP.
 * A repeated START between address and data would start a second addressed
 * transaction, so the device would not see a normal register write.
 */
void Twi2DevRegLngWr(uint8_t dev, uint32_t reg, uint8_t reg_len, uint8_t* val,
                     uint16_t val_len) {
  uint16_t i;
  uint8_t n = 0u;

  if (!twi2_open) {
    return;
  }
  if (reg_len == 1u) {
    twi2_lng_buf[n++] = (uint8_t)(reg & 0xFFu);
  }
  else if (reg_len == 2u) {
    twi2_lng_buf[n++] = (uint8_t)((reg >> 8) & 0xFFu);
    twi2_lng_buf[n++] = (uint8_t)(reg & 0xFFu);
  }
  else if (reg_len == 4u) {
    twi2_lng_buf[n++] = (uint8_t)((reg >> 24) & 0xFFu);
    twi2_lng_buf[n++] = (uint8_t)((reg >> 16) & 0xFFu);
    twi2_lng_buf[n++] = (uint8_t)((reg >> 8) & 0xFFu);
    twi2_lng_buf[n++] = (uint8_t)(reg & 0xFFu);
  }
  else {
    return;
  }
  if ((uint32_t)n + val_len > TWI2_LNG_MAX || (val_len > 0u && val == NULL)) {
    return;
  }
  for (i = 0u; i < val_len; ++i) {
    twi2_lng_buf[n + i] = val[i];
  }
  adi_twi_SetHardwareAddress(twi2_dev, dev);
  adi_twi_Write(twi2_dev, twi2_lng_buf, (uint32_t)n + val_len, false);
};

void Twi2Stop(void) {
  if (!twi2_open) {
    return;
  }
  adi_twi_Close(twi2_dev);
  twi2_open = 0u;
};
