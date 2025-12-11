#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

#define FRAMEBUFFER_WIDTH 80
#define FRAMEBUFFER_HEIGHT 25

static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;

void enable_cursor(void) {
    out(CURSOR_PORT_CMD, 0x0A);
    out(CURSOR_PORT_DATA, (in(CURSOR_PORT_DATA) & 0xC0) | 0xE);
    out(CURSOR_PORT_CMD, 0x0B);
    out(CURSOR_PORT_DATA, (in(CURSOR_PORT_DATA) & 0xE0) | 0xF);
}

void putchar (char c, uint8_t fg, uint8_t bg) {
    if (c == '\0') {
        return;
    }
    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            framebuffer_write(cursor_row, cursor_col, ' ', fg, bg);
            framebuffer_set_cursor(cursor_row, cursor_col);
        }
        return;
    }
    if (c == '\n') {
        cursor_row++;
        cursor_col = 0;
    } else {
        framebuffer_write(cursor_row, cursor_col, c, fg, bg);
        cursor_col++;
        if (cursor_col >= FRAMEBUFFER_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }
    if (cursor_row >= FRAMEBUFFER_HEIGHT) {
        // SCROLL UP - copy each row to the previous row
        // Note: FRAMEBUFFER_MEMORY_OFFSET is uint16_t*, so index = row * WIDTH + col
        for (uint8_t row = 1; row < FRAMEBUFFER_HEIGHT; row++) {
            for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++) {
                uint16_t src_pos = row * FRAMEBUFFER_WIDTH + col;
                uint16_t dst_pos = (row - 1) * FRAMEBUFFER_WIDTH + col;
                FRAMEBUFFER_MEMORY_OFFSET[dst_pos] = FRAMEBUFFER_MEMORY_OFFSET[src_pos];
            }
        }
        // CLEAR LAST LINE
        for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++) {
            uint16_t pos = (FRAMEBUFFER_HEIGHT - 1) * FRAMEBUFFER_WIDTH + col;
            uint8_t color = (bg << 4) | (fg & 0x0F);
            FRAMEBUFFER_MEMORY_OFFSET[pos] = (color << 8) | ' ';
        }
        cursor_row = FRAMEBUFFER_HEIGHT - 1;
    }
    framebuffer_set_cursor(cursor_row, cursor_col);
}

void puts(char *str, uint32_t char_count, uint8_t fg, uint8_t bg) {
    for (uint32_t i = 0; i < char_count; i++) {
        if (str[i] == '\0') {
            break;
        }
        putchar(str[i], fg, bg);
    }    
}

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    // TODO : Implement
    uint16_t pos = r * FRAMEBUFFER_WIDTH + c;

    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, (uint8_t) (pos & 0xFF));
    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, (uint8_t) ((pos >> 8) & 0XFF));
}

void framebuffer_write_string(uint8_t row, uint8_t col, const char *str, uint8_t fg, uint8_t bg) {
    uint8_t curr_row = row;
    uint8_t curr_col = col;
    
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            curr_row++;
            curr_col = 0;
        } else {
            framebuffer_write(curr_row, curr_col, str[i], fg, bg);
            curr_col++;
            if (curr_col >= 80) {
                curr_col = 0;
                curr_row++;
            }
        }
    }
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    // TODO : Implement
    uint16_t pos = row * 80 + col;
    uint8_t color = (bg << 4) | fg;
    uint16_t *fb = (uint16_t *) FRAMEBUFFER_MEMORY_OFFSET;
    fb[pos] = (color << 8) | c;
}

void framebuffer_clear(void) {
    uint16_t *fb = (uint16_t *) FRAMEBUFFER_MEMORY_OFFSET; // Mengambil alamat memory framebuffer
    for (uint16_t i = 0; i < 80 * 25; i++) {
        fb[i] = (0x07 << 8) | 0x00;
    }
    framebuffer_set_cursor(0,0);
}

void clear_screen(void) {
    framebuffer_clear();
    cursor_row = 0;
    cursor_col = 0;
}