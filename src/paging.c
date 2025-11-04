#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"

__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory = {
    .table = {
        [0] = {
            .flag.present_bit       = 1,
            .flag.write_bit         = 1,
            .flag.use_pagesize_4_mb = 1,
            .lower_address          = 0,
        },
        [0x300] = {
            .flag.present_bit       = 1,
            .flag.write_bit         = 1,
            .flag.use_pagesize_4_mb = 1,
            .lower_address          = 0,
        },
    }
};

static struct PageManagerState page_manager_state = {
    .page_frame_map = {[0 ... PAGE_FRAME_MAX_COUNT-1] = false}, 
    .free_page_frame_count = PAGE_FRAME_MAX_COUNT
    // TODO: Initialize page manager state properly
};

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr, 
    void *virtual_addr, 
    struct PageDirectoryEntryFlag flag
) {
    uint32_t page_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    page_dir->table[page_index].flag          = flag;
    page_dir->table[page_index].lower_address = ((uint32_t) physical_addr >> 22) & 0x3FF;
    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    asm volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr): "memory");
}



/* --- Memory Management --- */
// TODO: Implement
bool paging_allocate_check(uint32_t amount) {
    if (amount > page_manager_state.free_page_frame_count*PAGE_FRAME_SIZE) return false;
    return true;
}


bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    for (uint32_t i = 0; i<PAGE_FRAME_MAX_COUNT; i++){
        if (page_manager_state.page_frame_map[i] == false){
            struct PageDirectoryEntryFlag flag = {
                .present_bit = true,
                .write_bit = true,
                .user_bit = true,
                .use_pagesize_4_mb = true
            };
            update_page_directory_entry(page_dir, (void *)(i * PAGE_FRAME_SIZE), virtual_addr, flag);
            page_manager_state.page_frame_map[i] = true;
            page_manager_state.free_page_frame_count--;
            return true;
        }
    }
    return true;
    /**
     * TODO: Find free physical frame and map virtual frame into it 
     * - Find free physical frame in page_manager_state.page_frame_map[] using any strategies
     * - Mark page_manager_state.page_frame_map[]
     * - Update page directory with user flags:
     *     > present bit    true
     *     > write bit      true
     *     > user bit       true
     *     > pagesize 4 mb  true
     *
     **/
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t page_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    struct PageDirectoryEntryFlag flag = {
        .present_bit = false,
        .write_bit = false,
        .user_bit = false,
        .use_pagesize_4_mb = false
    };

    page_dir->table[page_index].flag = flag;
    page_dir->table[page_index].lower_address = 0;
    page_manager_state.page_frame_map[page_index] = false;
    page_manager_state.free_page_frame_count++;
    flush_single_tlb(virtual_addr);
    /* 
     * TODO: Deallocate a physical frame from respective virtual address
     * - Use the page_dir.table values to check mapped physical frame
     * - Remove the entry by setting it into 0
     */
    return true;
}

