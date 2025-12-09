global process_context_switch

process_context_switch:
    ; TODO : Implement
    ; Take and save context contents
    mov ecx, [esp + 4]

    ; index
    mov edi, [ecx + 0x0] ; move edi to curr register
    mov esi, [ecx + 0x4] ; move esi to curr register

    ; stack
    mov ebp, [ecx + 0x8]
    mov esp, [ecx + 0xc]

    ; general
    mov ebx, [ecx + 0x10]
    mov edx, [ecx + 0x14]
    mov eax, [ecx + 0x1c]

    ; segment
    mov gs, [ecx + 0x20]
    mov fs, [ecx + 0x24]
    mov es, [ecx + 0x28]
    mov ds, [ecx + 0x2c]

    mov ecx, [ecx + 0x18]
    iret