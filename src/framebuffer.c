#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

#define FRAMEBUFFER_WIDTH 80
#define FRAMEBUFFER_HEIGHT 25

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    // TODO : Implement
    uint16_t pos = r * FRAMEBUFFER_WIDTH + c;

    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, (uint8_t) (pos & 0xFF));
    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, (uint8_t) ((pos >> 8) & 0XFF));
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    // TODO : Implement
    uint16_t attribute = (bg << 4) | (fg & 0x0F);
    volatile uint16_t * where;
    where = (volatile uint16_t *)0xB8000 + (row * FRAMEBUFFER_WIDTH + col);
    *where = c | (attribute << 8);
}

void framebuffer_clear(void) {
    // TODO : Implement
    memset(FRAMEBUFFER_MEMORY_OFFSET, 0x00, FRAMEBUFFER_WIDTH*FRAMEBUFFER_HEIGHT*2);
}