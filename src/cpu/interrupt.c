#include "header/cpu/interrupt/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/driver/speaker.h"
#include "header/cpu/gdt.h"
#include "header/filesystem/ext2.h"
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"

struct TSSEntry _interrupt_tss_entry = {
    .ss0  = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};


void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    // Starts the initialization sequence in cascade mode
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); 
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET); // ICW2: Master PIC vector offset
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET); // ICW2: Slave PIC vector offset
    io_wait();
    out(PIC1_DATA, 0b0100); // ICW3: tell Master PIC, slave PIC at IRQ2 (0000 0100)
    io_wait();
    out(PIC2_DATA, 0b0010); // ICW3: tell Slave PIC its cascade identity (0000 0010)
    io_wait();

    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();

    // Disable all interrupts
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case 14:
            __asm__("hlt");
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case 0x30:
            syscall(frame);
            break;
    }
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}


void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame instead esp
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    // Add 8 because 4 for ret address and other 4 is for stack_ptr variable
    _interrupt_tss_entry.esp0 = stack_ptr + 8; 
}

void syscall(struct InterruptFrame frame) {
    // puts("SYSCALL CALLED\n", 0xE, 0xF, 0x0); //DEBUGGGGGGGGGGGGGG
    switch (frame.cpu.general.eax) {
        case 0:
            *((int8_t*) frame.cpu.general.ecx) = read(
                *(struct EXT2DriverRequest*) frame.cpu.general.ebx
            );
            break;
        case 1:
            *((int8_t*) frame.cpu.general.ecx) = read_directory(
                (struct EXT2DriverRequest*) frame.cpu.general.ebx
            );
            break;
        case 2:
            {
                struct EXT2DriverRequest kernel_request;
                struct EXT2DriverRequest *user_request = (struct EXT2DriverRequest*) frame.cpu.general.ebx;
                
                memcpy(&kernel_request, user_request, sizeof(struct EXT2DriverRequest));
                
                uint8_t kernel_buffer[BLOCK_SIZE * 16];
                if (kernel_request.buffer_size > 0 && kernel_request.buffer_size <= sizeof(kernel_buffer)) {
                    memcpy(kernel_buffer, kernel_request.buf, kernel_request.buffer_size);

                    kernel_request.buf = kernel_buffer;
                }
                
                *((int8_t*) frame.cpu.general.ecx) = write(&kernel_request);
            }
            break;
        case 3:
            *((int8_t*) frame.cpu.general.ecx) = delete(
                *(struct EXT2DriverRequest*) frame.cpu.general.ebx
            );
            break;
        case 4: { // getchar - BLOCKING version
            // Busy-wait sampai ada input
            while (keyboard_state.keyboard_buffer == '\0') {
                // Yield CPU - enable interrupt sebentar
                __asm__ volatile("sti; hlt");
            }
            
            // Ambil karakter
            *((char*)frame.cpu.general.ebx) = keyboard_state.keyboard_buffer;
            keyboard_state.keyboard_buffer = '\0';
            break;
        }
        case 5:
            putchar(
                (char) frame.cpu.general.ebx,
                (uint8_t) frame.cpu.general.ecx,
                0x0
            );
            break;
        case 6: {
            char *str = (char*) frame.cpu.general.ebx;
            uint32_t len = frame.cpu.general.ecx;
            uint8_t color = (uint8_t) frame.cpu.general.edx;
            
            // Print character by character karena puts() mungkin bermasalah
            for (uint32_t i = 0; i < len && str[i] != '\0'; i++) {
                putchar(str[i], color, 0x0);
            }
            break;
        }
        case 7: 
            keyboard_state_activate();
            break;
        case 8: // get_inode
        {
            struct EXT2DriverRequest *req = (struct EXT2DriverRequest *)frame.cpu.general.ebx;
            uint32_t *result_inode = (uint32_t *)frame.cpu.general.ecx;
            int8_t *retcode_ptr = (int8_t *)frame.cpu.general.edx;
            
            int8_t retcode = get_inode(*req, result_inode);
            *retcode_ptr = retcode;
            break;
        }

        case 9: // get_resolved_path
        {
            struct EXT2DriverRequest *req = (struct EXT2DriverRequest *)frame.cpu.general.ebx;
            char *result_path = (char *)frame.cpu.general.ecx;
            int8_t *retcode_ptr = (int8_t *)frame.cpu.general.edx;
            
            int8_t retcode = get_resolved_path(*req, result_path);
            *retcode_ptr = retcode;
            break;
        }
        case 10:
        {
            clear_screen();
            break;
        }
        case 11: // speaker_beep syscall
        {
            uint16_t frequency = (uint16_t) frame.cpu.general.ebx;
            uint32_t duration = frame.cpu.general.ecx;
            speaker_beep(frequency, duration);
            break;
        }
    }
}