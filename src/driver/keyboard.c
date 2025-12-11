#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

const char keyboard_scancode_1_to_ascii_map_shifted[256] = {
      0, 0x1B, '!', '@', '#', '$', '%', '^',  '&', '*', '(',  ')',  '_', '+', '\b', '\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',  'O', 'P', '{',  '}', '\n',   0,  'A',  'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',   0,  '|',  'Z', 'X',  'C',  'V',
    'B',  'N', 'M', '<', '>', '?',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on = false,
    .keyboard_buffer = '\0',
    .shift_pressed = false,
    .ctrl_pressed = false,
    .ctrl_c_pressed = false,
};

void keyboard_state_activate() {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void){
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = '\0';
}

bool is_shift_presseed(void) {
    return keyboard_state.shift_pressed;
}

bool is_ctrl_c_pressed(void) {
    bool result = keyboard_state.ctrl_c_pressed;
    keyboard_state.ctrl_c_pressed = false;  // Clear the flag after reading
    return result;
}

bool is_ctrl_pressed(void) {
    return keyboard_state.ctrl_pressed;
}

bool is_shift_pressed(void) {
    return keyboard_state.shift_pressed;
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);
    bool is_break = scancode & 0x80;
    uint8_t key = scancode & 0x7F;

    if (keyboard_state.keyboard_input_on) {
        if (scancode == EXTENDED_SCANCODE_BYTE) {
            keyboard_state.read_extended_mode = true;
        } else {
            if (keyboard_state.read_extended_mode) {
                if (!is_break) {
                    switch (key) {
                        case EXT_SCANCODE_UP:
                            keyboard_state.keyboard_buffer = KEY_UP;
                            break;
                        case EXT_SCANCODE_DOWN:
                            keyboard_state.keyboard_buffer = KEY_DOWN;
                            break;
                        case EXT_SCANCODE_LEFT:
                            keyboard_state.keyboard_buffer = KEY_LEFT;
                            break;
                        case EXT_SCANCODE_RIGHT:
                            keyboard_state.keyboard_buffer = KEY_RIGHT;
                            break;
                        case SCANCODE_RCTRL:
                            keyboard_state.ctrl_pressed = true;
                            break;
                    }
                } else if (key == SCANCODE_RCTRL) {
                    keyboard_state.ctrl_pressed = false;
                }
                keyboard_state.read_extended_mode = false;
            } else {
                if (key == SCANCODE_LSHIFT || key == SCANCODE_RSHIFT) {
                    keyboard_state.shift_pressed = !is_break;
                } else if (key == SCANCODE_LCTRL) {
                    keyboard_state.ctrl_pressed = !is_break;
                } else if (!is_break) {
                    // Check for Ctrl+C - tapi HANYA jika tidak ada Shift (untuk memungkinkan Ctrl+Shift+C)
                    if (keyboard_state.ctrl_pressed && !keyboard_state.shift_pressed && key == SCANCODE_C) {
                        keyboard_state.ctrl_c_pressed = true;
                    } else {
                        char ascii_char;
                        if (keyboard_state.shift_pressed) {
                            ascii_char = keyboard_scancode_1_to_ascii_map_shifted[key];
                        } else {
                            ascii_char = keyboard_scancode_1_to_ascii_map[key];
                        }
                        
                        if (ascii_char != '\0') {
                            __asm__ volatile("" ::: "memory");
                            keyboard_state.keyboard_buffer = ascii_char;
                            __asm__ volatile("" ::: "memory");
                        }
                    }
                }
            }
        }
    }

    pic_ack(IRQ_KEYBOARD);
}