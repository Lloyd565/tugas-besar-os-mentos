#ifndef _SPEAKER_H
#define _SPEAKER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Speaker driver for system beep using the PC speaker
 * The PC speaker is controlled through:
 * - Port 0x61: System Control Port B (speaker control)
 * - Port 0x40-0x42: PIT (Programmable Interval Timer) channels
 */

#define PIT_CHANNEL_0       0x40
#define PIT_CHANNEL_1       0x41
#define PIT_CHANNEL_2       0x42
#define PIT_CONTROL_PORT    0x43
#define SYSTEM_CONTROL_PORT 0x61

#define PIT_FREQUENCY 1193180  // PIT base frequency in Hz
#define MAX_FREQUENCY 4000     // Maximum safe frequency for PC speaker
#define MIN_FREQUENCY 20       // Minimum safe frequency for PC speaker

/**
 * speaker_beep - Make a beep sound with specified frequency and duration
 * @param frequency Frequency in Hz (20-4000 Hz is safe range)
 * @param duration Duration in milliseconds
 */
void speaker_beep(uint16_t frequency, uint32_t duration);

/**
 * speaker_beep_simple - Make a simple beep with default frequency
 * @param duration Duration in milliseconds
 */
void speaker_beep_simple(uint32_t duration);

#endif
