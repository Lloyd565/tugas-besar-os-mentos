#ifndef _FRAMEBUFFER_H
#define _FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define FRAMEBUFFER_MEMORY_OFFSET ((uint16_t*) 0xC00B8000)
#define CURSOR_PORT_CMD    0x03D4
#define CURSOR_PORT_DATA   0x03D5
#define FRAMEBUFFER_WIDTH 80
#define FRAMEBUFFER_HEIGHT 25

/* Text mode color constants */
// Regular colors (0-7)
#define FB_BLACK          0x0
#define FB_BLUE           0x1
#define FB_GREEN          0x2
#define FB_CYAN           0x3
#define FB_RED            0x4
#define FB_MAGENTA        0x5
#define FB_BROWN          0x6
#define FB_LIGHT_GRAY     0x7

// Bright colors (8-15)
#define FB_DARK_GRAY      0x8
#define FB_BRIGHT_BLUE    0x9
#define FB_BRIGHT_GREEN   0xA
#define FB_BRIGHT_CYAN    0xB
#define FB_BRIGHT_RED     0xC
#define FB_BRIGHT_MAGENTA 0xD
#define FB_YELLOW         0xE
#define FB_WHITE          0xF

/**
 * Terminal framebuffer
 * Resolution: 80x25
 * Starting at FRAMEBUFFER_MEMORY_OFFSET,
 * - Even number memory: Character, 8-bit
 * - Odd number memory:  Character color lower 4-bit, Background color upper 4-bit
*/

/**
 * Set framebuffer character and color with corresponding parameter values.
 * More details: https://en.wikipedia.org/wiki/BIOS_color_attributes
 *
 * @param row Vertical location (index start 0)
 * @param col Horizontal location (index start 0)
 * @param c   Character
 * @param fg  Foreground / Character color
 * @param bg  Background color
 */
void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);

/**
 * Set cursor to specified location. Row and column starts from 0
 * 
 * @param r row
 * @param c column
*/


void framebuffer_write_string(uint8_t row, uint8_t col, const char *str, uint8_t fg, uint8_t bg);

void framebuffer_set_cursor(uint8_t r, uint8_t c);

/**
 * Set all cell in framebuffer character to 0x00 (empty character)
 * and color to 0x07 (gray character & black background)
 * Extra note: It's allowed to use different color palette for this
 *
 */
void framebuffer_clear(void);

void putchar(char c, uint8_t fg, uint8_t bg);
void puts(char *str, uint32_t char_count, uint8_t fg, uint8_t bg);
void move_text_cursor(uint8_t r, uint8_t c);
void clear_screen(void);

#endif