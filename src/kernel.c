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
    // framebuffer_write_string(1, 0, "among us",0xA,0x0);
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    framebuffer_write_string(0, 0, "among us",0xA,0x0);
    initialize_filesystem_ext2();
<<<<<<< HEAD
=======
    framebuffer_write_string(1, 0, "among us",0xA,0x0);
>>>>>>> cf6b78a2c14c5804a7fee600ebbe075aa7f152a8
    gdt_install_tss();
    framebuffer_write_string(2, 0, "among us",0xA,0x0);
    set_tss_register();
    framebuffer_write_string(3, 0, "among us",0xA,0x0);
<<<<<<< HEAD
    // initialize_filesystem_ext2();
=======

>>>>>>> cf6b78a2c14c5804a7fee600ebbe075aa7f152a8
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

    uint8_t* fix_ptr = (uint8_t*) 0x0;

    if (fix_ptr[4] == 0x08) {
        fix_ptr[4] = 0x00;
        fix_ptr[5] = 0xEB;
    }
    // Set TSS $esp pointer and jump into shell 
    set_tss_kernel_current_stack();
    framebuffer_write_string(4, 0, "among us",0xA,0x0);
    kernel_execute_user_program((uint8_t*) 0);
    framebuffer_write_string(5, 0, "among us",0xA,0x0);
    while (true);
}
