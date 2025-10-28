#include <stdint.h>

uint8_t in(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void out(uint16_t port, uint8_t data) {
    __asm__ volatile ("outb %0, %1" : : "a"(data), "Nd"(port));
}


void out16(uint16_t port, uint16_t data) {
    __asm__(
        "outw %0, %1"
        : // <Empty output operand>
        : "a"(data), "Nd"(port)
    );
}

uint16_t in16(uint16_t port) {
    uint16_t result;
    __asm__ volatile(
        "inw %1, %0"
        : "=a"(result)
        : "Nd"(port)
    );
    return result;
}
