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

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    keyboard_state_activate(); 
    
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    initialize_filesystem_ext2();
    
    gdt_install_tss();
    set_tss_register();
    paging_allocate_user_page_frame(&_paging_kernel_page_directory, (uint8_t*) 0);
    uint32_t root_inode = 2;
    
    struct EXT2DriverRequest dir_req = {
        .buf            = (void *)0,
        .name           = "docs",
        .parent_inode   = root_inode,
        .buffer_size    = 0,
        .name_len       = 4,
        .is_directory   = true
    };
    write(&dir_req);

    char *readme_content = "Welkam to MentOS!\nThis is a simple operating system.\nYou can use: ls, cat, grep, find, touch\nTry: cat testfile.txt\nOr: cat testfile.txt | grep line\n";
    struct EXT2DriverRequest readme_req = {
        .buf            = (uint8_t *)readme_content,
        .name           = "readme.txt",
        .parent_inode   = root_inode,
        .buffer_size    = 152,
        .name_len       = 10,
        .is_directory   = false
    };
    write(&readme_req);
    struct EXT2DriverRequest request = {
        .buf                   = (uint8_t*) 0,
        .name                  = "shell",
        .parent_inode          = 2,
        .buffer_size           = 0x100000,
        .name_len              = 5
    };


    int8_t retcode = read(request);

    if (retcode == 0) {
        set_tss_kernel_current_stack();
        kernel_execute_user_program((uint8_t*) 0);
        
    } else {
        
        char rc_display = '0' + retcode;
        if (retcode < 0) {
            framebuffer_write(19, 7, '-', 0xC, 0x0);
            rc_display = '0' + (-retcode);
            framebuffer_write(20, 7, rc_display, 0xC, 0x0);
        } else {
            framebuffer_write(19, 7, rc_display, 0xC, 0x0);
        }
        
        if (retcode == 1) {
            framebuffer_write_string(0, 8, "Error: Not a file", 0xC, 0x0);
        } else if (retcode == 2) {
            framebuffer_write_string(0, 8, "Error: Buffer too small", 0xC, 0x0);
        } else if (retcode == 3) {
            framebuffer_write_string(0, 8, "Error: File not found", 0xC, 0x0);
        } else if (retcode == 4) {
            framebuffer_write_string(0, 8, "Error: Parent not dir", 0xC, 0x0);
        }
        
        while(true);
    }
    
    while (true);
}