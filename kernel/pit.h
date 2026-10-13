#ifndef PIT_H
#define PIT_H

#include <stdint.h>

#define PIT_DEFAULT_HZ 100 /* 100 Hz = 10 ms interval */

/*
 * Initialize the 8254 PIT timer to fire IRQ0 at target_hz.
 */
void pit_init(uint32_t target_hz);

/*
 * Get the current monotonically increasing system tick counter.
 */
uint64_t timer_get_ticks(void);

/*
 * Query the timer frequency in Hz.
 */
uint32_t timer_get_frequency(void);

#endif /* PIT_H */
