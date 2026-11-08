/*
 * A2B master on the AD2428MINI expansion board, called AD24280 here.
 * Init opens TWI2, plays the discovery script, then closes TWI2.
 * The monitor reads interrupts and bit errors from the main loop and reports them.
 * The SPORT2 wires stay in place. There is no second expansion board.
 */

#ifndef AD2428_H_
#define AD2428_H_

#ifdef __cplusplus
extern "C" {
#endif

void Ad24280Mon(void);
void Ad24280MonReq(void);
void Ad24280Init(void);

#ifdef __cplusplus
}
#endif

#endif
