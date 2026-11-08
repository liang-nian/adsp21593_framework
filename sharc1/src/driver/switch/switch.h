/*
 * Board GPIO. Powers up ADAU1962 and ADAU1979 and releases reset.
 * Each write is followed by a TIME_BND spin.
 */

#ifndef SWITCH_H_
#define SWITCH_H_

#ifdef __cplusplus
extern "C" {
#endif

void Sw0Init(void);

#ifdef __cplusplus
}
#endif

#endif
