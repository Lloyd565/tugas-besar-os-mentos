#include "header/driver/speaker.h"
#include "header/cpu/portio.h"

/**
 * Delay for a specified number of milliseconds
 * This is a simple busy-wait delay
 */
static void delay_ms(uint32_t ms) {
    // Increased delay multiplier for better timing on QEMU
    // Each iteration is roughly a few nanoseconds on QEMU
    for (uint32_t i = 0; i < ms * 55000; i++) {
        __asm__ volatile("nop");
    }
}

/**
 * speaker_beep - Make a beep sound with specified frequency and duration
 * @param frequency Frequency in Hz
 * @param duration Duration in milliseconds
 */
void speaker_beep(uint16_t frequency, uint32_t duration) {
    // Clamp frequency to safe range
    if (frequency < MIN_FREQUENCY) frequency = MIN_FREQUENCY;
    if (frequency > MAX_FREQUENCY) frequency = MAX_FREQUENCY;

    // Calculate the divisor for PIT
    // Divisor = Base Frequency / Desired Frequency
    uint16_t divisor = PIT_FREQUENCY / frequency;

    // Step 1: Send the command byte to PIT control port
    // Command: 0xB6 = 10110110
    // - Bits 7-6: 10 (Channel 2)
    // - Bits 5-4: 11 (Access mode: lobyte/hibyte)
    // - Bits 3-1: 011 (Mode 3: Square wave generator)
    // - Bit 0: 0 (Binary count)
    out(PIT_CONTROL_PORT, 0xB6);

    // Step 2: Send the divisor to PIT Channel 2 (port 0x42)
    // Send low byte first, then high byte
    out(PIT_CHANNEL_2, (uint8_t)(divisor & 0xFF));
    out(PIT_CHANNEL_2, (uint8_t)((divisor >> 8) & 0xFF));

    // Step 3: Enable the speaker
    // Read current value from System Control Port
    uint8_t speaker_control = in(SYSTEM_CONTROL_PORT);
    // Set bit 1 (enable speaker) and bit 0 (enable timer 2)
    speaker_control |= 0x03;
    out(SYSTEM_CONTROL_PORT, speaker_control);

    // Step 4: Wait for the specified duration
    delay_ms(duration);

    // Step 5: Disable the speaker
    speaker_control = in(SYSTEM_CONTROL_PORT);
    // Clear bit 1 (disable speaker) and bit 0
    speaker_control &= ~0x03;
    out(SYSTEM_CONTROL_PORT, speaker_control);
}

/**
 * speaker_beep_simple - Make a simple beep with default frequency (1000 Hz)
 * @param duration Duration in milliseconds
 */
void speaker_beep_simple(uint32_t duration) {
    speaker_beep(1000, duration);
}
