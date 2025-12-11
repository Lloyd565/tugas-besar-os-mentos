#ifndef _MOUSE_H
#define _MOUSE_H

#include <stdint.h>
#include <stdbool.h>

// PS/2 Mouse Ports
#define MOUSE_DATA_PORT     0x60
#define MOUSE_STATUS_PORT   0x64
#define MOUSE_COMMAND_PORT  0x64

// PS/2 Mouse Commands
#define MOUSE_SET_DEFAULT   0xF6
#define MOUSE_ENABLE        0xF4
#define MOUSE_DISABLE       0xF5
#define MOUSE_RESET         0xFF
#define MOUSE_ACK           0xFA

// Status port bits
#define MOUSE_STATUS_OUTPUT_BUFFER  0x01
#define MOUSE_STATUS_INPUT_BUFFER   0x02

// Mouse Button Flags (from mouse data packet)
#define MOUSE_LEFT_BUTTON    0x01
#define MOUSE_RIGHT_BUTTON   0x02
#define MOUSE_MIDDLE_BUTTON  0x04

/**
 * MouseDriverState - Mouse driver state structure
 */
struct MouseDriverState {
    uint32_t x;           // X coordinate (0-based, max 1024)
    uint32_t y;           // Y coordinate (0-based, max 768)
    uint8_t buttons;      // Button states (bit 0=left, bit 1=right, bit 2=middle)
    bool mouse_input_on;  // Is mouse interrupt enabled
    uint8_t packet_index; // Current packet byte being read (0-2)
    uint8_t packet[3];    // Mouse data packet buffer
    bool click_occurred;  // Flag for click detection
    // Drag tracking for text selection
    bool drag_active;     // Is user currently dragging (left button down)
    uint32_t drag_start_x; // Start position of drag (in character grid, not pixels)
    uint32_t drag_start_y;
    uint32_t drag_end_x;   // Current end position of drag
    uint32_t drag_end_y;
} __attribute__((packed));

extern struct MouseDriverState mouse_state;

/* -- Mouse Driver Interfaces -- */

/**
 * Initialize PS/2 mouse
 */
void mouse_init(void);

/**
 * Enable mouse input/interrupts
 */
void mouse_state_activate(void);

/**
 * Disable mouse input/interrupts
 */
void mouse_state_deactivate(void);

/**
 * Get current mouse position and button state
 * Returns: button state (bit 0=left, bit 1=right, bit 2=middle)
 */
uint8_t mouse_get_state(uint32_t *x, uint32_t *y);

/**
 * Get and clear click flag
 */
bool mouse_get_click(void);

/**
 * Mouse ISR - handle mouse interrupt
 */
void mouse_isr(void);

#endif
