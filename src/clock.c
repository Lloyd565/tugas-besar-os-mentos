
// No includes used. Native types only.

// Define types similar to stdint
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

struct Time {
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char day;
    unsigned char month;
    unsigned int year;
};

// Syscall wrapper
void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : : "r"(eax));
    __asm__ volatile("int $0x30");
}

void syscall_get_time(struct Time *t) {
    syscall(24, (uint32_t)t, 0, 0);  // Syscall 24 = get_time
}

void syscall_puts_at(char *str, uint32_t len, uint8_t color, uint8_t row, uint8_t col) {
    uint32_t combined = color | (row << 8) | (col << 16);
    syscall(25, (uint32_t)str, len, combined);  // Syscall 25 = puts_at
}

void int_to_str(int n, char *buf) {
    if (n < 10) {
        buf[0] = '0';
        buf[1] = n + '0';
    } else {
        buf[0] = (n / 10) + '0';
        buf[1] = (n % 10) + '0';
    }
}

int main(void) {
    struct Time t;
    char time_str[9]; // HH:MM:SS\0
    time_str[2] = ':';
    time_str[5] = ':';
    time_str[8] = '\0';
    
    // Position: Bottom Right (Row 24, Col 70)
    // Framebuffer is 80x25 (0-24 rows, 0-79 cols)
    
    while (1) {
        syscall_get_time(&t);
        
        int_to_str(t.hour, &time_str[0]);
        int_to_str(t.minute, &time_str[3]);
        int_to_str(t.second, &time_str[6]);
        
        syscall_puts_at(time_str, 8, 0xF, 24, 71); // White color
        
        // Busy wait delay
        for (volatile int i = 0; i < 100000; i++) {}
    }
    
    return 0;
}
