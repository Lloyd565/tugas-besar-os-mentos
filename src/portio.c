#include <stdint.h>

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
