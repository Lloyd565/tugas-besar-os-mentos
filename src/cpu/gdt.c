#include "header/cpu/gdt.h"

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
        }
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
