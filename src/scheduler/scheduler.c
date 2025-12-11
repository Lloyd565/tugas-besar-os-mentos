#include "header/scheduler/scheduler.h"
#include "header/process/process.h"
#include "header/cpu/interrupt/interrupt.h"


extern struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

/**
 * Read all general purpose register values and set control register.
 * Resume the execution flow back to ctx.eip and ctx.eflags
 * 
 * @note          Implemented in assembly
 * @param context Target context to switch into
 */
__attribute__((noreturn)) extern void process_context_switch(struct Context ctx);
    


/* --- Scheduler --- */
/**
 * Initialize scheduler before executing init process 
 */
void scheduler_init(void) { activate_timer_interrupt(); }

/**
 * Save context to current running process
 * 
 * @param ctx Context to save to current running process control block
 */
void scheduler_save_context_to_current_running_pcb(struct Context ctx){
    struct ProcessControlBlock* currPCB = process_get_current_running_pcb_pointer();
    if (currPCB != NULL) { currPCB->context = ctx; }
        
}

/**
 * Trigger the scheduler algorithm and context switch to new process
 * 
 * FIX for bootloop:
 * - Add iteration counter to prevent infinite loop if no READY process found
 * - Validate page_directory is not NULL before context switch
 * - If no process found after full iteration, idle in hlt loop (should not happen in normal operation)
 */
__attribute__((noreturn)) void scheduler_switch_to_next_process(void){
    uint32_t next = -1;
    uint32_t iteration_count = 0;
    const uint32_t MAX_ITERATIONS = PROCESS_COUNT_MAX * 2;  // Allow 2 full cycles to find READY process

    // Cek running pcb, kalau ada yang lagi ngerun = stop
    struct ProcessControlBlock *currPCB = process_get_current_running_pcb_pointer();
    if (currPCB != NULL){
        currPCB->metadata.state = READY;
        currPCB->metadata.is_active = true;  // FIX: Keep is_active true for READY processes
        next = currPCB->metadata.pid % PROCESS_COUNT_MAX;
    }

    while (iteration_count < MAX_ITERATIONS){
        next = (next + 1) % PROCESS_COUNT_MAX;
        iteration_count++;

        if (_process_list[next].metadata.state == READY && _process_list[next].metadata.is_active){
            struct ProcessControlBlock *nextPCB = &(_process_list[next]);

            // FIX: Validate page_directory is not NULL before using it
            if (nextPCB->context.page_directory_virtual_addr == NULL) {
                // Should not happen, but skip this process if page_directory is invalid
                continue;
            }

            nextPCB->metadata.state = RUNNING;
            nextPCB->metadata.is_active = true;

            // FIX for Issue #3: Update TSS.esp0 before context switch
            // update_tss_kernel_stack();

            paging_use_page_directory(nextPCB->context.page_directory_virtual_addr);
            process_context_switch(nextPCB->context);
            // process_context_switch is __noreturn, so we never reach here
        }
    }

    // If we reach here, no READY process was found - this should not happen in normal operation
    // Idle in kernel space by repeatedly halting
    while (true) {
        __asm__ volatile("hlt");
    }
}