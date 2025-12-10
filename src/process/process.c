#include "header/process/process.h"
#include "header/memory/paging.h"
#include "header/stdlib/string.h"
#include "header/cpu/gdt.h"


struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX] = {0};
struct ProcessState process_manager_state = {0};

struct ProcessControlBlock* process_get_current_running_pcb_pointer(void){
    for (int i = 0; i< PROCESS_COUNT_MAX; i++){
        if (_process_list[i].metadata.state == RUNNING) return &(_process_list[i]);
    }
    return NULL;
}
int32_t ceil_div(uint32_t a, uint32_t b) {
    return (a+b-1)/b;
}

uint32_t process_list_get_inactive_index() {
    for (int i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (!(_process_list[i].metadata.is_active)) {
            return i;
        }
    }
    return -1;
}

uint32_t process_generate_new_pid() {
    for (int i = 0; i < PROCESS_COUNT_MAX; i++){
        if (!_process_list[i].metadata.is_active)
        {
            return i;
        }
    }    
    return -1;
}

int32_t process_create_user_process(struct EXT2DriverRequest request) {
    int32_t retcode = PROCESS_CREATE_SUCCESS; 
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    // Ensure entrypoint is not located at kernel's section at higher half
    if ((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    // Check whether memory is enough for the executable and additional frame for user stack
    uint32_t page_frame_count_needed = ceil_div(request.buffer_size + PAGE_FRAME_SIZE, PAGE_FRAME_SIZE);
    if (!paging_allocate_check(page_frame_count_needed) || page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // Process PCB 
    int32_t p_index = process_list_get_inactive_index();
    struct ProcessControlBlock *new_pcb = &(_process_list[p_index]);

    // DEBUG
    // char buf[32];
    // sprintf(buf, "p_index=%d\n", p_index);
    // puts(buf, strlen(buf), 0x0F, 0x00);
    // 

    if (p_index < 0){
        // Tidak ada slot kosong
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    new_pcb->context.page_directory_virtual_addr = paging_create_new_page_directory();
    
    // FIX: Validate page_directory allocation success
    if (new_pcb->context.page_directory_virtual_addr == NULL) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }
    
    // allocate user page frame

    paging_allocate_user_page_frame(new_pcb->context.page_directory_virtual_addr, (uint8_t *) 0);
    paging_allocate_user_page_frame(new_pcb->context.page_directory_virtual_addr, (uint8_t *) 0xBFFFFFFC);
    // temporarily change the page directory to the new process

    char name_buf[PROCESS_NAME_LENGTH_MAX];
    memcpy(name_buf, request.name, request.name_len);
    request.name = name_buf;
    struct PageDirectory *old_page_directory = paging_get_current_page_directory_addr();
    paging_use_page_directory(new_pcb->context.page_directory_virtual_addr);
    
    // read and load the executable into the new process
    read(request);
    // restore the old page directory
    paging_use_page_directory(old_page_directory);
    
    // prepare state and context
    new_pcb->context.eip = 0;
    new_pcb->context.cpu = (struct CPURegister) {0};
    new_pcb->context.cpu.stack.ebp = 0xBFFFFFFC;
    new_pcb->context.cpu.stack.esp = 0xBFFFFFFC;
    new_pcb->context.cpu.segment.ds = 0x23;
    new_pcb->context.cpu.segment.es = 0x23;
    new_pcb->context.cpu.segment.fs = 0x23;
    new_pcb->context.cpu.segment.gs = 0x23;
    new_pcb->context.eflags = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    new_pcb->metadata.pid = process_generate_new_pid();
    new_pcb->metadata.state = READY;
    new_pcb->metadata.is_active = true;  // FIX: Set is_active immediately
    
    // FIX for Issue #4: Validate critical fields
    if (new_pcb->context.page_directory_virtual_addr == NULL) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }
    // Note: EIP=0 is valid as it's the entry point in user virtual space
    
    memcpy(new_pcb->metadata.name, name_buf, request.name_len);
    new_pcb->metadata.name[request.name_len] = '\0';
    process_manager_state.active_process_count++;

exit_cleanup:
    return retcode;
}