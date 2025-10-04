#include "header/cpu/interrupt/idt.h"
#include "header/cpu/gdt.h"

struct {
    struct IDTGate table[IDT_MAX_ENTRY_COUNT];
} interrupt_descriptor_table;
struct IDTGate _idt[IDT_MAX_ENTRY_COUNT];
struct IDTR _idt_idtr;

void initialize_idt(void) {
    /* 
     * TODO: 
     * Iterate all isr_stub_table,
     * Set all IDT entry with set_interrupt_gate()
     * with following values:
     * Vector: i
     * Handler Address: isr_stub_table[i]
     * Segment: GDT_KERNEL_CODE_SEGMENT_SELECTOR
     * Privilege: 0
     */
    for (uint8_t i = 0; i < ISR_STUB_TABLE_LIMIT; i++)
    {
        set_interrupt_gate(i, isr_stub_table[i], GDT_KERNEL_CODE_SEGMENT_SELECTOR, 0);
    }
    _idt_idtr.limit = sizeof(interrupt_descriptor_table) - 1;
    _idt_idtr.base_address = (uint32_t)&interrupt_descriptor_table;
    __asm__ volatile("lidt %0" : : "m"(_idt_idtr));
    __asm__ volatile("sti");
}

void set_interrupt_gate(
    uint8_t  int_vector, 
    void     *handler_address, 
    uint16_t gdt_seg_selector, 
    uint8_t  privilege
) {
    struct IDTGate *idt_int_gate = &interrupt_descriptor_table.table[int_vector];
    // TODO : Set handler offset, privilege & segment
    // Use &-bitmask, bitshift, and casting for offset 
    uintptr_t handler = (uintptr_t) handler_address;

    // Target system 32-bit and flag this as valid interrupt gate
    idt_int_gate->offset_low  = (uint16_t) handler & 0xFFFF;          // Lower 16-bit
    idt_int_gate->segment     = gdt_seg_selector;
    idt_int_gate->_reserved   = 0;
    // P = 1           (Present bit)
    // DPL = privilege (CPU  Privilege Levels)
    // S = 0           (System segment)
    // GateType=1110   (32-bit interrupt gate)
    idt_int_gate->type        = 0x8E | ((privilege & 0x3) << 5);
    idt_int_gate->offset_high = (uint16_t) (handler >> 16) & 0xFFFF; // Upper 16-bit
}
