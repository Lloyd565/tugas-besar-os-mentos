#include <stdint.h>
#include "header/filesystem/ext2.h"

#define BLOCK_COUNT 16

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : /* <Empty> */ : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : /* <Empty> */ : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : /* <Empty> */ : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : /* <Empty> */ : "r"(eax));
    // Note : gcc usually use %eax as intermediate register,
    //        so it need to be the last one to mov
    __asm__ volatile("int $0x30");
}

void read(uint32_t request_ptr, uint32_t retcode_ptr) {
    syscall(0, request_ptr, retcode_ptr, 0);
}

void read_directory(uint32_t request_ptr, uint32_t retcode_ptr) {
    syscall(1, request_ptr, retcode_ptr, 0);
}

void write(uint32_t request_ptr, uint32_t retcode_ptr) {
    syscall(2, request_ptr, retcode_ptr, 0);
}

void delete(uint32_t request_ptr, uint32_t retcode_ptr) {
    syscall(3, request_ptr, retcode_ptr, 0);
}

void putchar(uint32_t c, uint8_t color, uint8_t bg_color) {
    syscall(5, c, color, bg_color);
}

void puts(char *buf, uint8_t color, uint8_t bg_color) {
    syscall(6, (uint32_t) buf, buf, bg_color);
}


int main(void) {
    struct BlockBuffer      bl[2]   = {0};
    struct EXT2DriverRequest request = {
        .buf                   = &bl,
        .name                  = "shell",
        .parent_inode                 = 2,
        .buffer_size           = BLOCK_SIZE * BLOCK_COUNT,
        .name_len = 5,
    };
    
    int32_t retcode;
    syscall(0, (uint32_t) &request, (uint32_t) &retcode, 0);
    if (retcode == 0)
        syscall(6, (uint32_t) "owo\n", 4, 0xF);

    char buf;
    syscall(7, 0, 0, 0);
    while (true) {
        syscall(4, (uint32_t) &buf, 0, 0);
        syscall(5, (uint32_t) &buf, 0xF, 0);
    }

    return 0;
}