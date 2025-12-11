#include "header/cpu/interrupt/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/driver/mouse.h"
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
        case PIC2_OFFSET + IRQ_MOUSE:
            mouse_isr();
            break;
        case 0x30:
            syscall(frame);
            break;
    }
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void activate_mouse_interrupt(void) {
    out(PIC2_DATA, in(PIC2_DATA) & ~(1 << IRQ_MOUSE));
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
        case 4: { // getchar - with mouse polling capability
            // Non-blocking getchar with small timeout to allow mouse updates
            uint32_t timeout = 50000;  // Small timeout iterations
            
            while (keyboard_state.keyboard_buffer == '\0' && !keyboard_state.ctrl_c_pressed && timeout > 0) {
                // Enable interrupts and wait a tiny bit
                __asm__ volatile("sti");
                for (volatile uint32_t i = 0; i < 1000; i++);  // Small delay
                __asm__ volatile("cli");
                timeout--;
            }
            
            // If still no input, do a full HLT wait
            while (keyboard_state.keyboard_buffer == '\0' && !keyboard_state.ctrl_c_pressed) {
                __asm__ volatile("sti; hlt");
            }
            
            // Check apakah Ctrl+C yang ditekan
            if (keyboard_state.ctrl_c_pressed) {
                keyboard_state.ctrl_c_pressed = false;
                *((char*)frame.cpu.general.ebx) = 0x03;  // Return Ctrl+C character (ASCII 3)
            } else {
                // Ambil karakter
                *((char*)frame.cpu.general.ebx) = keyboard_state.keyboard_buffer;
                keyboard_state.keyboard_buffer = '\0';
            }
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
            puts(
                (char*) frame.cpu.general.ebx,
                (uint32_t) frame.cpu.general.ecx,
                (uint8_t) frame.cpu.general.edx,
                0x0
            );
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
        case 12: // is_ctrl_c_pressed syscall
        {
            bool result = is_ctrl_c_pressed();
            *((bool*)frame.cpu.general.ebx) = result;
            break;
        }
        case 13: // mouse_init syscall
        {
            mouse_init();
            activate_mouse_interrupt();
            break;
        }
        case 14: // mouse_get_state syscall
        {
            uint32_t *x = (uint32_t*)frame.cpu.general.ebx;
            uint32_t *y = (uint32_t*)frame.cpu.general.ecx;
            uint8_t *buttons = (uint8_t*)frame.cpu.general.edx;
            *buttons = mouse_get_state(x, y);
            break;
        }
        case 15: // mouse_get_click syscall
        {
            bool result = mouse_get_click();
            *((bool*)frame.cpu.general.ebx) = result;
            break;
        }
        case 16: // render_mouse_pointer syscall - draw mouse blocking text
        {
            uint32_t x = frame.cpu.general.ebx;
            uint32_t y = frame.cpu.general.ecx;
            uint8_t color = (uint8_t)frame.cpu.general.edx;
            
            // Convert pixel coordinates to character grid (80x24)
            uint8_t col = x / 8;  // Assuming 8 pixels per character width
            uint8_t row = y / 8;  // Assuming 8 pixels per character height
            
            if (col < 80 && row < 24) {
                framebuffer_write(row, col, '*', color, 0x00);
            }
            break;
        }
        case 17: // is_ctrl_pressed syscall
        {
            bool result = is_ctrl_pressed();
            *((bool*)frame.cpu.general.ebx) = result;
            break;
        }
        case 18: // is_shift_pressed syscall
        {
            bool result = is_shift_pressed();
            *((bool*)frame.cpu.general.ebx) = result;
            break;
        }
        case 19: // get_mouse_drag_state syscall
        {
            // ebx = &drag_active, ecx = &start_x, edx = &start_y
            // Note: need 4 more registers for end_x and end_y, so return via structure
            // For now, just return whether drag is active
            bool *drag_active_ptr = (bool*)frame.cpu.general.ebx;
            uint32_t *coords = (uint32_t*)frame.cpu.general.ecx; // Array: [start_x, start_y, end_x, end_y]
            
            *drag_active_ptr = mouse_state.drag_active;
            if (coords) {
                coords[0] = mouse_state.drag_start_x;
                coords[1] = mouse_state.drag_start_y;
                coords[2] = mouse_state.drag_end_x;
                coords[3] = mouse_state.drag_end_y;
            }
            break;
        }
        case 20: // render_selection syscall - highlight selected text
        {
            // For now, this is a placeholder syscall
            // Selection rendering is handled by the shell directly
            break;
        }
    }
}