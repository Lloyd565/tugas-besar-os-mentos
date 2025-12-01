#include "header/cpu/gdt.h"
#include "header/cpu/interrupt/interrupt.h"
/**
 * global_descriptor_table, predefined GDT.
 * Initial SegmentDescriptor already set properly according to Intel Manual & OSDev.
 * Table entry : [{Null Descriptor}, {Kernel Code}, {Kernel Data (variable, etc)}, ...].
 */
struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        {
            .segment_low   = 0x0000,
            .base_low = 0x0000,
            .base_mid = 0x00,
            .type_bit = 0x0,
            .non_system = 0,
            .dpl = 0x0,
            .present = 0x0,
            .segment_high = 0x0,
            .avl = 0x0,
            .long_mode = 0x0,
            .db = 0x0,
            .gran = 0x0,
            .base_high = 0x00
        },
        {
            .segment_low  = 0xFFFF,
            .base_low = 0X0000,
            .base_mid = 0x00,
            .type_bit = 0xA,
            .non_system = 1,
            .dpl = 0x0,
            .present = 1,
            .segment_high = 0xF,
            .avl = 0x0,
            .long_mode = 0x0,
            .db = 1,
            .gran = 1,
            .base_high = 0x00
        },
        {
            .segment_low = 0xFFFF,
            .base_low = 0x0000,
            .base_mid = 0x00,
            .type_bit = 0x2,
            .non_system = 1,
            .dpl = 0x0,
            .present = 1,
            .segment_high = 0xF,
            .avl = 0x0,
            .long_mode = 0x0,
            .db = 1,
            .gran = 1,
            .base_high = 0x00
        },
                {
            .segment_low  = 0xFFFF,
            .base_low = 0X0000,
            .base_mid = 0x00,
            .type_bit = 0xA,
            .non_system = 1,
            .dpl = 0x3,
            .present = 1,
            .segment_high = 0xF,
            .avl = 0x0,
            .long_mode = 0x0,
            .db = 1,
            .gran = 1,
            .base_high = 0x00
        },
        {
            .segment_low = 0xFFFF,
            .base_low = 0x0000,
            .base_mid = 0x00,
            .type_bit = 0x2,
            .non_system = 1,
            .dpl = 0x3,
            .present = 1,
            .segment_high = 0xF,
            .avl = 0x0,
            .long_mode = 0x0,
            .db = 1,
            .gran = 1,
            .base_high = 0x00
        },
        {
            .segment_low = sizeof(struct TSSEntry),
            .base_low = 0x0000,
            .base_mid = 0x00,
            .type_bit = 0x9,
            .non_system = 0,
            .dpl = 0x0,
            .present = 1,
            .segment_high = (sizeof(struct TSSEntry) & (0xF << 16)) >> 16,
            .avl = 0,
            .long_mode = 0x0,
            .db = 1,
            .gran = 0,
            .base_high = 0x00
        },
        {0}
    }
};

/**
 * _gdt_gdtr, predefined system GDTR. 
 * GDT pointed by this variable is already set to point global_descriptor_table above.
 * From: https://wiki.osdev.org/Global_Descriptor_Table, GDTR.size is GDT size minus 1.
 */
struct GDTR _gdt_gdtr = {
    // TODO : Implement, this GDTR will point to global_descriptor_table. 
    //        Use sizeof operator
    .size = sizeof(global_descriptor_table) - 1,
    .address = &global_descriptor_table
};

void gdt_install_tss(void) {
    uint32_t base = (uint32_t) &_interrupt_tss_entry;
    global_descriptor_table.table[5].base_high = (base & (0xFF << 24)) >> 24;
    global_descriptor_table.table[5].base_mid  = (base & (0xFF << 16)) >> 16;
    global_descriptor_table.table[5].base_low  = base & 0xFFFF;
}
