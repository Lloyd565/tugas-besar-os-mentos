global process_context_switch
; process_context_switch 
process_context_switch:
    ; TODO : Implement

    ; STEP 1 : SAVE CONTEXT
    ; Take and save context contents
    lea  ecx, [esp + 0x04]
    
    ; STEP 2 : iret tech
    push 0x23 ; Stack segment selector (GDT_USER_DATA_SELECTOR), user privilege
    mov  eax, [ecx + 0xc] ; esp
    push eax
    mov  eax, [ecx + 0x34] ; eflags
    push eax
    mov  eax, 0x18 | 0x3 ; Code Segment
    push eax ; Code segment selector (GDT_USER_CODE_SELECTOR), user privilege
    mov  eax, [ecx + 0x30] ; eip
    push eax



    ; index
    mov edi, [ecx + 0x0] ; move edi to curr register
    mov esi, [ecx + 0x4] ; move esi to curr register

    ; stack
    mov ebp, [ecx + 0x8]
    ; mov esp, [ecx + 0xc]

    ; general
    mov ebx, [ecx + 0x10]
    mov edx, [ecx + 0x14]

    ; segment
    mov gs, [ecx + 0x20]
    mov fs, [ecx + 0x24]
    mov es, [ecx + 0x28]
    mov ds, [ecx + 0x2c]

    ; cr3, virtual_page_dir
    mov eax, [ecx + 0x38]
    mov cr3, eax

    ; rest of general
    mov eax, [ecx + 0x1c]
    mov ecx, [ecx + 0x18]

    iret