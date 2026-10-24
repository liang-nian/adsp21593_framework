#ifndef AD2428_H_
#define AD2428_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set to 1 only while SigmaStudio USBi is the external TWI2 master.
 * Firmware must already have started DAC BCLK/FS. Ad24280Init then
 * leaves TWI2 closed so the probe and the on-chip master do not contend.
 * Default 0: the on-chip master replays ad24280_cfg.
 */
#ifndef AD2428_USBI_OWNS_TWI
#define AD2428_USBI_OWNS_TWI 0
#endif

void Ad2428ClkReady(uint8_t ready);
void Ad24280Mon(void);
void Ad24281Mon(void);
void Ad24280Init(void);
void Ad24281Init(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AD2428_H_
