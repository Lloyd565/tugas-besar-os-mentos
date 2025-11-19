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
        // SCROLL UP
        for (uint8_t row = 1; row < FRAMEBUFFER_HEIGHT; row++) {
            for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++) {
                FRAMEBUFFER_MEMORY_OFFSET[(row - 1) * FRAMEBUFFER_WIDTH*2 + col*2] =
                    FRAMEBUFFER_MEMORY_OFFSET[row * FRAMEBUFFER_WIDTH*2 + col*2];
                FRAMEBUFFER_MEMORY_OFFSET[(row - 1) * FRAMEBUFFER_WIDTH*2 + col*2 + 1] =
                    FRAMEBUFFER_MEMORY_OFFSET[row * FRAMEBUFFER_WIDTH*2 + col*2 + 1];
            }
        }
        // CLEAR LAST LINE
        for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++)
        {
            FRAMEBUFFER_MEMORY_OFFSET[(FRAMEBUFFER_HEIGHT - 1) * FRAMEBUFFER_WIDTH*2 + col*2] = ' ';
            FRAMEBUFFER_MEMORY_OFFSET[(FRAMEBUFFER_HEIGHT - 1) * FRAMEBUFFER_WIDTH*2 + col*2 + 1] = (bg << 4) | (fg & 0x0F);
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
    // TODO : Implement
    memset(FRAMEBUFFER_MEMORY_OFFSET, 0x00, FRAMEBUFFER_WIDTH*FRAMEBUFFER_HEIGHT*2);
}