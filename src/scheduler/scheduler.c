#include "header/scheduler/scheduler.h"
#include "header/process/process.h"


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
 */
__attribute__((noreturn)) void scheduler_switch_to_next_process(void){
    uint32_t next = -1;
    // // Cek running pcb, kalau ada yang lagi ngerun = stop
    struct ProcessControlBlock *currPCB = process_get_current_running_pcb_pointer();
    if (currPCB != NULL){
        currPCB->metadata.state = READY;
        currPCB->metadata.is_active = false;
        next = currPCB->metadata.pid % PROCESS_COUNT_MAX;
    }
    while (true){
        next = (next + 1) % PROCESS_COUNT_MAX;
        if (_process_list[next].metadata.state == READY){
            struct ProcessControlBlock *nextPCB = &(_process_list[next]);
            nextPCB->metadata.state = RUNNING;
            nextPCB->metadata.is_active = true;
            paging_use_page_directory(nextPCB->context.page_directory_virtual_addr);
            process_context_switch(nextPCB->context);
        }
    }
}
