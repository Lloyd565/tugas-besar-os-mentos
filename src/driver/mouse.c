#include "header/driver/mouse.h"
#include "header/cpu/portio.h"
#include "header/text/framebuffer.h"

struct MouseDriverState mouse_state = {
    .x = 512,
    .y = 384,
    .buttons = 0,
    .mouse_input_on = false,
    .packet_index = 0,
    .click_occurred = false,
    .drag_active = false,
    .drag_start_x = 0,
    .drag_start_y = 0,
    .drag_end_x = 0,
    .drag_end_y = 0,
};

void mouse_init(void) {
    // Ultra-simple mouse initialization - just set flag
    // Don't do any hardware access that might block
    mouse_state.mouse_input_on = true;
    mouse_state.packet_index = 0;
    
    // Small delay
    for (volatile uint32_t i = 0; i < 10000; i++);
}

void mouse_state_activate(void) {
    mouse_state.mouse_input_on = true;
}

void mouse_state_deactivate(void) {
    mouse_state.mouse_input_on = false;
}

uint8_t mouse_get_state(uint32_t *x, uint32_t *y) {
    *x = mouse_state.x;
    *y = mouse_state.y;
    return mouse_state.buttons;
}

bool mouse_get_click(void) {
    bool result = mouse_state.click_occurred;
    mouse_state.click_occurred = false;
    return result;
}

void mouse_isr(void) {
    if (!mouse_state.mouse_input_on) return;
    
    uint8_t data = in(MOUSE_DATA_PORT);
    
    if (mouse_state.packet_index == 0) {
        // First byte - contains button and sign bits
        mouse_state.packet[0] = data;
        
        // Check if this is a valid first byte (bit 3 should always be 1)
        if (!(data & 0x08)) return;
        
        // Store button state
        uint8_t prev_buttons = mouse_state.buttons;
        mouse_state.buttons = data & 0x07;
        
        // Detect click (transition from no-button to button-pressed)
        if (prev_buttons == 0 && mouse_state.buttons != 0) {
            mouse_state.click_occurred = true;
        }
        
        // Track left button drag for text selection
        if (mouse_state.buttons & MOUSE_LEFT_BUTTON) {
            // Left button is pressed
            if (!mouse_state.drag_active) {
                // Start new drag - convert pixel coords to character grid (80x25)
                mouse_state.drag_active = true;
                mouse_state.drag_start_x = (mouse_state.x * 80) / 1024;  // Correct scaling
                mouse_state.drag_start_y = (mouse_state.y * 25) / 768;   // Correct scaling
                if (mouse_state.drag_start_x > 79) mouse_state.drag_start_x = 79;
                if (mouse_state.drag_start_y > 24) mouse_state.drag_start_y = 24;
            }
        } else {
            // Left button released
            mouse_state.drag_active = false;
        }
        
        mouse_state.packet_index = 1;
    } else if (mouse_state.packet_index == 1) {
        // Second byte - X movement
        mouse_state.packet[1] = data;
        mouse_state.packet_index = 2;
    } else if (mouse_state.packet_index == 2) {
        // Third byte - Y movement
        mouse_state.packet[2] = data;
        mouse_state.packet_index = 0;
        
        // Process complete packet
        int8_t dx = (int8_t)mouse_state.packet[1];
        int8_t dy = (int8_t)mouse_state.packet[2];
        
        // Update position with boundaries
        int32_t new_x = (int32_t)mouse_state.x + dx;
        int32_t new_y = (int32_t)mouse_state.y - dy;  // Y is inverted
        
        // Clamp to screen bounds (assuming 1024x768)
        if (new_x < 0) new_x = 0;
        if (new_x > 1023) new_x = 1023;
        if (new_y < 0) new_y = 0;
        if (new_y > 767) new_y = 767;
        
        mouse_state.x = (uint32_t)new_x;
        mouse_state.y = (uint32_t)new_y;
        
        // Update drag end position if dragging
        if (mouse_state.drag_active) {
            mouse_state.drag_end_x = (mouse_state.x * 80) / 1024;
            mouse_state.drag_end_y = (mouse_state.y * 25) / 768;
            if (mouse_state.drag_end_x > 79) mouse_state.drag_end_x = 79;
            if (mouse_state.drag_end_y > 24) mouse_state.drag_end_y = 24;
        }
    }
}
