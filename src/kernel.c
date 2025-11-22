#include <stdint.h>
#include <stdbool.h>
#include "header/cpu/gdt.h"
#include "header/text/framebuffer.h"
#include "header/kernel-entrypoint.h"
#include "header/cpu/interrupt/interrupt.h"
#include "header/cpu/interrupt/idt.h"
#include "header/driver/keyboard.h"
#include "header/driver/disk.h"
#include "header/filesystem/ext2.h"
#include "header/memory/paging.h"

// void kernel_setup(void) {
//     load_gdt(&_gdt_gdtr);
//     pic_remap();
//     initialize_idt();
//     activate_keyboard_interrupt();
//     framebuffer_clear();
//     framebuffer_set_cursor(0, 0);
//     initialize_filesystem_ext2();
//     gdt_install_tss();
//     set_tss_register();

//     // Allocate first 4 MiB virtual memory
//     paging_allocate_user_page_frame(&_paging_kernel_page_directory, (uint8_t*) 0);

//     // Write shell into memory
//     struct EXT2DriverRequest request = {
//         .buf                   = (uint8_t*) 0,
//         .name                  = "shell",
//         .parent_inode          = 1,
//         .buffer_size           = 0x100000,
//         .name_len              = 5,
//     };
//     read(request);

//     // Set TSS $esp pointer and jump into shell 
//     set_tss_kernel_current_stack();
//     kernel_execute_user_program((uint8_t*) 0);

//     while (true);
// }
void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    // framebuffer_write_string(0, 0, "among us",0xA,0x0);
    initialize_filesystem_ext2();
    framebuffer_write_string(1, 0, "among us",0xA,0x0);
    gdt_install_tss();
    framebuffer_write_string(2, 0, "among us",0xA,0x0);
    set_tss_register();
    framebuffer_write_string(3, 0, "among us",0xA,0x0);

    // Allocate first 4 MiB virtual memory
    paging_allocate_user_page_frame(&_paging_kernel_page_directory, (uint8_t*) 0);

    // Write shell into memory
    struct EXT2DriverRequest request = {
        .buf                   = (uint8_t*) 0,
        .name                  = "shell",
        .parent_inode          = 2,
        .buffer_size           = 0x100000,
        .name_len              = 5,
    };
    read(request);

    // Set TSS $esp pointer and jump into shell 
    set_tss_kernel_current_stack();
    kernel_execute_user_program((uint8_t*) 0);

    while (true);
}
