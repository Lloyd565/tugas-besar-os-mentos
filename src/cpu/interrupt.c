#include "header/cpu/interrupt/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/cpu/gdt.h"
#include "header/filesystem/ext2.h"
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/scheduler/scheduler.h"
#include "header/scheduler/scheduler.h"
#include "header/process/process.h"
#include "../cmos.h"

extern struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

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
        case PIC1_OFFSET + IRQ_TIMER:
            pic_ack(IRQ_TIMER);
            timer_isr(frame);
            // scheduler_switch_to_next_process();
            break;
        case 14:
            __asm__("hlt");
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:
            pic_ack(IRQ_KEYBOARD);
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

/**
 * FIX for Issue #3: Update TSS.esp0 based on current kernel stack
 * This must be called to update the kernel stack pointer in TSS
 * for proper interrupt handling on privilege level transitions.
 * 
 * This is called during context switches to ensure TSS points to
 * the correct kernel stack for the next interrupt.
 */
void update_tss_kernel_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame - current ESP at entry of this function
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    // Add 8 because 4 for ret address and other 4 is for stack_ptr variable
    _interrupt_tss_entry.esp0 = stack_ptr + 8;
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
        case 4: { // getchar - NON-BLOCKING version
            *((char*)frame.cpu.general.ebx) = keyboard_state.keyboard_buffer;
            if (keyboard_state.keyboard_buffer != '\0') {
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
        case 11 : //kill process
        {
            int32_t pid = *(int32_t*)frame.cpu.general.ebx;
            
            struct ProcessControlBlock *curr = process_get_current_running_pcb_pointer();
            bool is_self = (curr != NULL && (int32_t)curr->metadata.pid == pid);
            
            if (process_destroy(pid)) {
                *(int8_t*)frame.cpu.general.ecx = 0; // Success
                if (is_self) {
                    scheduler_switch_to_next_process();
                }
            } else {
                 *(int8_t*)frame.cpu.general.ecx = -1; // Failed
            }
            break;
        }
        case 12: // exec
        {
            struct EXT2DriverRequest *req = (struct EXT2DriverRequest*) frame.cpu.general.ebx;
            int8_t *ret = (int8_t*) frame.cpu.general.ecx;
            
            // 1. Resolve Inode to determine file size
            uint32_t inode = 0;
            if (get_inode(*req, &inode) != 0) {
                framebuffer_write_string(0, 0, "Exec: Inode not found", 0xC, 0x0);
                *ret = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
                break;
            }
            
            struct EXT2Inode inode_data;
            read_inode(inode, &inode_data);
            
            // Check if it is a file
            if (inode_data.i_mode & EXT2_S_IFDIR) {
                framebuffer_write_string(0, 0, "Exec: Is directory", 0xC, 0x0);
                *ret = 1; // Attempting to exec a directory
                break;
            }

            // 2. Set buffer_size for reading
            req->buffer_size = inode_data.i_size;
            
            // 3. Create process
            *ret = process_create_user_process(*req);
            if (*ret != 0) {
                 char buf[2] = {*ret + '0', '\0'};
                 framebuffer_write_string(0, 50, "Exec: Create fail code:", 0xC, 0x0);
                 framebuffer_write_string(0, 75, buf, 0xC, 0x0);
            }
            break;
        }
        case 13: // ps
        {
            uint32_t index = (uint32_t) frame.cpu.general.ebx;
            void *output_buffer = (void*) frame.cpu.general.ecx;
            
            if (index < PROCESS_COUNT_MAX) {
                // Only copy the metadata part (pid, state, name, is_active)
                memcpy(output_buffer, &(_process_list[index].metadata), sizeof(_process_list[index].metadata));
            } else {
                // If index invalid, we can't safely zero the user buffer easily without knowing size, 
                // but for this specific syscall structure, we expect user to loop correctly.
                // Or we can zero out the sizeof metadata.
                memset(output_buffer, 0, sizeof(_process_list[index].metadata)); 
            }
            break;
        }
        case 14: // get_time
        {
            struct Time *time_ptr = (struct Time*) frame.cpu.general.ebx;
            cmos_get_time(time_ptr);
            break;
        }
        case 15: // puts at (puts_at)
        {
            char *str = (char*) frame.cpu.general.ebx;
            // uint32_t len = (uint32_t) frame.cpu.general.ecx;
            // uint32_t color = (uint32_t) frame.cpu.general.edx; // Using edx for color
            // Need row and col. We can pack them or use stack/struct. 
            // Let's assume eax, ebx, ecx, edx are used. 
            // We need 5 args: str, len, color, row, col.
            // Let's pack row/col into esi or just use struct?
            // User shell follows linux syscall convention (registers).
            // Let's use: ebx=str, ecx=len, edx=color | row << 8 | col
            // edx is 32 bit. 0x00RRGGBB? No.
            // Let's do: ebx=str, ecx=len, edx=color (8 bit), esi=row, edi=col?
            // interrupt frame might not capture esi/edi easily if not in general struct.
            // Frame struct has cpu.general which likely has esi/edi.
            // Check InterruptFrame definition? It usually has pushe-d general regs.
            // Assuming we only have eax, ebx, ecx, edx available easily in the switch?
            // Let's look at `syscall(6, ...)` which was `puts`.
            // case 6: puts(ebx, ecx, edx).
            // Let's modify frame.cpu.general.edx to hold color (low 8), row (next 8), col (next 8).
            // edx = color | (row << 8) | (col << 16).
            
            uint32_t combined = frame.cpu.general.edx;
            uint8_t c = combined & 0xFF;
            uint8_t r = (combined >> 8) & 0xFF;
            uint8_t co = (combined >> 16) & 0xFF;
            
            framebuffer_write_string(r, co, str, c, 0); // Background 0 implied? Or pass it?
            break;
        }
    }
}

void activate_timer_interrupt(void) {
    __asm__ volatile("cli");
    // Setup how often PIT fire
    uint32_t pit_timer_counter_to_fire = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (pit_timer_counter_to_fire & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) ((pit_timer_counter_to_fire >> 8) & 0xFF));

    // Activate the interrupt
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
    
}

void timer_isr(struct InterruptFrame frame){
    // get context to save
    struct ProcessControlBlock *curr_pcb = process_get_current_running_pcb_pointer();
    
    if (curr_pcb == NULL) {
        // No running process (maybe destroyed), switch immediately
        scheduler_switch_to_next_process();
    } else if (curr_pcb->metadata.state == RUNNING && (frame.int_stack.cs & 0x3) == 0x3){
        struct Context ctx = {
            .cpu = frame.cpu,
            // .cpu.stack.esp = frame.cpu.stack.esp,
            .eip = frame.int_stack.eip,
            .eflags = frame.int_stack.eflags,
            .esp = frame.cpu.stack.esp,//*((uint32_t*) &frame.int_stack + 4),
            .page_directory_virtual_addr = paging_get_current_page_directory_addr()
        };
        // framebuffer_write_string(5, 0, "context saved", 0, 0);
        //save context, schedule for next process
        // __asm__ volatile("cli");
        scheduler_save_context_to_current_running_pcb(ctx);
        scheduler_switch_to_next_process();
    // framebuffer_write_string(6, 0, "scheduled next process", 0, 0);
    }
}


