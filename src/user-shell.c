#include <stdint.h>           // uint32_t, int8_t, dll
#include <stdbool.h>          // bool, true, false
#include "header/filesystem/ext2.h"  // EXT2 structures
#include "header/stdlib/string.h"    // memset, memcmp
#include "header/text/framebuffer.h"

#define PROCESS_NAME_LENGTH_MAX 32
#define PROCESS_COUNT_MAX 16

typedef enum PROCESS_STATE {
    READY,
    RUNNING,
    BLOCKED,
    DESTROYED
} PROCESS_STATE;

struct ProcessInfo {
    uint32_t pid;
    PROCESS_STATE state;
    char name[PROCESS_NAME_LENGTH_MAX];
    bool is_active;
};

#define BLOCK_COUNT 16
#define INPUT_BUFFER_SIZE 256
#define MAX_ARGS 8

// Syscall wrapper functions - dengan prefix syscall_ untuk menghindari conflict
void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : : "r"(eax));
    __asm__ volatile("int $0x30");
}

void syscall_read(struct EXT2DriverRequest *request, int8_t *retcode) {
    syscall(0, (uint32_t)request, (uint32_t)retcode, 0);
}

void syscall_read_directory(struct EXT2DriverRequest *request, int8_t *retcode) {
    syscall(1, (uint32_t)request, (uint32_t)retcode, 0);
}

void syscall_write(struct EXT2DriverRequest *request, int8_t *retcode) {
    syscall(2, (uint32_t)request, (uint32_t)retcode, 0);
}

void syscall_delete(struct EXT2DriverRequest *request, int8_t *retcode) {
    syscall(3, (uint32_t)request, (uint32_t)retcode, 0);
}

void syscall_getchar(char *buf) {
    char c = 0;
    while (c == 0) {
        syscall(4, (uint32_t)&c, 0, 0);
    }
    *buf = c;
}

void syscall_putchar(char c, uint8_t color) {
    syscall(5, (uint32_t)c, color, 0);
}

void syscall_puts(char *str, uint32_t len, uint8_t color) {
    syscall(6, (uint32_t)str, len, color);
}

void syscall_get_inode(struct EXT2DriverRequest *request, uint32_t *result_inode, int8_t *retcode) {
    syscall(8, (uint32_t)request, (uint32_t)result_inode, (uint32_t)retcode);
}

void syscall_get_resolved_path(struct EXT2DriverRequest *request, char *result_path, int8_t *retcode) {
    syscall(9, (uint32_t)request, (uint32_t)result_path, (uint32_t)retcode);
}

void syscall_activate_keyboard() {
    syscall(7, 0, 0, 0);
}

void syscall_kill(int32_t pid, int8_t *retcode) {
    syscall(11, (uint32_t)&pid, (uint32_t)retcode, 0);
}

void syscall_exec(struct EXT2DriverRequest *request, int8_t *retcode) {
    syscall(12, (uint32_t)request, (uint32_t)retcode, 0);
}

void syscall_get_process_info(uint32_t index, struct ProcessInfo *pcb) {
    syscall(13, index, (uint32_t)pcb, 0);
}

void syscall_puts_at(char *str, uint32_t len, uint8_t color, uint8_t row, uint8_t col) {
    uint32_t combined = color | (row << 8) | (col << 16);
    syscall(15, (uint32_t)str, len, combined);
}

// Shell state
struct ShellState {
    uint32_t current_dir_inode;
    char current_path[256];
} shell_state;

// Command structure
struct Command {
    char cmd[32];
    char args[MAX_ARGS][64];
    uint8_t argc;
};

// Pipeline support
#define PIPE_BUFFER_SIZE 4096
static char pipe_buffer[PIPE_BUFFER_SIZE];
static uint32_t pipe_buffer_len = 0;
static bool pipe_mode = false;

// Print to screen or pipe buffer
void print_or_pipe(char *str, uint8_t color) {
    if (pipe_mode) {
        // Append to pipe buffer
        uint32_t len = strlen(str);
        for (uint32_t i = 0; i < len && pipe_buffer_len < PIPE_BUFFER_SIZE - 1; i++) {
            pipe_buffer[pipe_buffer_len++] = str[i];
        }
        pipe_buffer[pipe_buffer_len] = '\0';
    } else {
        syscall_puts(str, strlen(str), color);
    }
}

void putchar_or_pipe(char c, uint8_t color) {
    if (pipe_mode) {
        if (pipe_buffer_len < PIPE_BUFFER_SIZE - 1) {
            pipe_buffer[pipe_buffer_len++] = c;
            pipe_buffer[pipe_buffer_len] = '\0';
        }
    } else {
        syscall_putchar(c, color);
    }
}

// Parse input into command and arguments
void parse_input(char *input, struct Command *cmd) {
    memset(cmd, 0, sizeof(struct Command));
    
    uint8_t i = 0;
    uint8_t arg_idx = 0;
    uint8_t char_idx = 0;
    bool in_cmd = true;
    
    while (input[i] != '\0' && input[i] != '\n') {
        if (input[i] == ' ') {
            if (in_cmd) {
                cmd->cmd[char_idx] = '\0';
                in_cmd = false;
                char_idx = 0;
            } else if (char_idx > 0) {
                cmd->args[arg_idx][char_idx] = '\0';
                arg_idx++;
                char_idx = 0;
                if (arg_idx >= MAX_ARGS) break;
            }
        } else {
            if (in_cmd) {
                if (char_idx < 31) {
                    cmd->cmd[char_idx++] = input[i];
                }
            } else {
                if (char_idx < 63) {
                    cmd->args[arg_idx][char_idx++] = input[i];
                }
            }
        }
        i++;
    }
    
    if (in_cmd) {
        cmd->cmd[char_idx] = '\0';
    } else if (char_idx > 0) {
        cmd->args[arg_idx][char_idx] = '\0';
        arg_idx++;
    }
    
    cmd->argc = arg_idx;
}

// Print string helper
void print(char *str, uint8_t color) {
    syscall_puts(str, strlen(str), color);
}
// Command: ls - list directory contents
void cmd_ls() {
    struct BlockBuffer buf;
    struct EXT2DriverRequest req = {
        .buf = &buf,
        .name = "",
        .name_len = 0,
        .parent_inode = shell_state.current_dir_inode,
        .buffer_size = BLOCK_SIZE
    };
    
    int8_t retcode;
    syscall_read_directory(&req, &retcode);
    
    if (retcode != 0) {
        print("Error reading directory\n", 0xC);
        return;
    }
    
    uint32_t offset = 0;
    struct EXT2DirectoryEntry *entry;
    
    // List all entries
    while (offset < BLOCK_SIZE) {
        entry = (struct EXT2DirectoryEntry *)(buf.buf + offset);
        if (entry->rec_len == 0 || entry->inode == 0) break;
        
        // Skip . and .. for display
        char *name = (char *)(entry + 1);
        if ((entry->name_len == 1 && name[0] == '.') ||
            (entry->name_len == 2 && name[0] == '.' && name[1] == '.')) {
            offset += entry->rec_len;
            continue;
        }
        
        char name_buf[256];
        memset(name_buf, 0, 256);
        for (uint8_t i = 0; i < entry->name_len; i++) {
            name_buf[i] = name[i];
        }
        
        uint8_t color = (entry->file_type == EXT2_FT_DIR) ? 0xB : 0xF;
        print(name_buf, color);
        
        if (entry->file_type == EXT2_FT_DIR) {
            print("/", 0xB);
        }
        print("  ", 0xF);
        
        offset += entry->rec_len;
    }
    print("\n", 0xF);
}
void cmd_cd(char *dirname) {
    if (dirname[0] == '\0') {
        shell_state.current_dir_inode = 2;
        strcpy(shell_state.current_path, "/");
        return;
    }
    
    if (strcmp(dirname, ".") == 0) {
        return;
    }
    // Step 1: Get the inode
    struct EXT2DriverRequest req = {
        .name = dirname,
        .name_len = strlen(dirname),
        .parent_inode = shell_state.current_dir_inode
    };
    
    uint32_t new_inode = 0;
    int8_t retcode = -1;
    syscall_get_inode(&req, &new_inode, &retcode);
    
    if (retcode != 0) {
        print("cd: directory not found\n", 0xC);
        return;
    }

    struct BlockBuffer check_buf;
    struct EXT2DriverRequest check_req = {
        .buf = &check_buf,
        .name = "",
        .name_len = 0,
        .parent_inode = new_inode,
        .buffer_size = BLOCK_SIZE
    };
    
    int8_t dir_check;
    syscall_read_directory(&check_req, &dir_check);
    
    if (dir_check != 0) {
        print("cd: not a directory\n", 0xC);
        return;
    }
    // Step 2: Get resolved path
    char new_path[256];
    memset(new_path, 0, 256);
    
    struct EXT2DriverRequest path_req = {
        .buf = shell_state.current_path,
        .name = dirname,
        .name_len = strlen(dirname)
    };
    
    retcode = -1;
    syscall_get_resolved_path(&path_req, new_path, &retcode);

    // Step 3: Update shell state
    shell_state.current_dir_inode = new_inode;
    
    if (new_path[0] != '\0') {
        strcpy(shell_state.current_path, new_path);
    } else {
        print("WARNING: path empty, using fallback\n", 0xE);
        strcpy(shell_state.current_path, "/?");
    }
    
}
// Command: mkdir - create directory
void cmd_mkdir(char *dirname) {
    if (dirname[0] == '\0') {
        print("mkdir: missing operand\n", 0xC);
        return;
    }
    
    struct BlockBuffer buf;
    struct EXT2DriverRequest req = {
        .buf = &buf,
        .name = dirname,
        .name_len = strlen(dirname),
        .parent_inode = shell_state.current_dir_inode,
        .buffer_size = 0,
        .is_directory = true
    };
    
    int8_t retcode;
    syscall_write(&req, &retcode);
    
    if (retcode == 0) {
        print("Directory created\n", 0xA);
    } else if (retcode == 1) {
        print("mkdir: directory already exists\n", 0xC);
    } else {
        print("mkdir: error creating directory\n", 0xC);
    }
}

// Command: cat - display file contents
void cmd_cat(char *filename) {
    if (filename[0] == '\0') {
        print("cat: missing operand\n", 0xC);
        return;
    }
    
    struct BlockBuffer buf[BLOCK_COUNT];
    struct EXT2DriverRequest req = {
        .buf = buf,
        .name = filename,
        .name_len = strlen(filename),
        .parent_inode = shell_state.current_dir_inode,
        .buffer_size = BLOCK_SIZE * BLOCK_COUNT
    };
    
    int8_t retcode;
    syscall_read(&req, &retcode);
    
    if (retcode == 0) {
        // Print file contents
        char *content = (char *)buf;
        for (uint32_t i = 0; i < BLOCK_SIZE * BLOCK_COUNT && content[i] != '\0'; i++) {
            putchar_or_pipe(content[i], 0xF);
        }
        print_or_pipe("\n", 0xF);
    } else if (retcode == 1) {
        print("cat: not a regular file\n", 0xC);
    } else if (retcode == 3) {
        print("cat: file not found\n", 0xC);
    } else {
        print("cat: error reading file\n", 0xC);
    }
}

// Command: cp - copy file
void cmd_cp(char *src, char *dest) {
    if (src[0] == '\0' || dest[0] == '\0') {
        print("cp: missing operand\n", 0xC);
        return;
    }
    
    // Read source file
    struct BlockBuffer buf[BLOCK_COUNT];
    struct EXT2DriverRequest read_req = {
        .buf = buf,
        .name = src,
        .name_len = strlen(src),
        .parent_inode = shell_state.current_dir_inode,
        .buffer_size = BLOCK_SIZE * BLOCK_COUNT
    };
    
    int8_t retcode;
    syscall_read(&read_req, &retcode);
    
    if (retcode != 0) {
        print("cp: cannot read source file\n", 0xC);
        return;
    }
    
    // Write to destination
    struct EXT2DriverRequest write_req = {
        .buf = buf,
        .name = dest,
        .name_len = strlen(dest),
        .parent_inode = shell_state.current_dir_inode,
        .buffer_size = BLOCK_SIZE * BLOCK_COUNT,
        .is_directory = false
    };
    
    syscall_write(&write_req, &retcode);
    
    if (retcode == 0) {
        print("File copied\n", 0xA);
    } else {
        print("cp: error copying file\n", 0xC);
    }
}

// Command: rm - remove file/directory
void cmd_rm(char *name) {
    if (name[0] == '\0') {
        print("rm: missing operand\n", 0xC);
        return;
    }
    
    struct EXT2DriverRequest req = {
        .name = name,
        .name_len = strlen(name),
        .parent_inode = shell_state.current_dir_inode
    };
    
    int8_t retcode;
    syscall_delete(&req, &retcode);
    
    if (retcode == 0) {
        print("Removed\n", 0xA);
    } else if (retcode == 1) {
        print("rm: file not found\n", 0xC);
    } else if (retcode == 2) {
        print("rm: directory not empty\n", 0xC);
    } else {
        print("rm: error removing\n", 0xC);
    }
}

// Command: mv - move/rename file
void cmd_mv(char *src, char *dest) {
    cmd_cp(src, dest);
    cmd_rm(src);
}

void cmd_echo (char *text) {
    print(text, 0xF);
    print("\n", 0xF);
}

void cmd_touch(char *filename) {
    if (filename[0] == '\0') {
        print("touch: missing operand\n", 0xC);
        return;
    }
    
    // Remove trailing slash if present
    uint32_t len = strlen(filename);
    if (len > 0 && filename[len - 1] == '/') {
        filename[len - 1] = '\0';
        len--;
    }
    
    // Create empty file buffer
    struct BlockBuffer buf;
    memset(&buf, 0, sizeof(buf));
    
    struct EXT2DriverRequest req = {
        .buf = &buf,
        .name = filename,
        .name_len = strlen(filename),
        .parent_inode = shell_state.current_dir_inode,
        .buffer_size = 0,  // Empty file
        .is_directory = false
    };
    
    int8_t retcode;
    syscall_write(&req, &retcode);
    
    if (retcode == 0) {
        print("touch: ", 0xA);
        print(filename, 0xA);
        print(" created\n", 0xA);
    } else if (retcode == 1) {
        print("touch: file already exists\n", 0xC);
    } else if (retcode == 2) {
        print("touch: invalid parent directory\n", 0xC);
    } else {
        print("touch: unknown error\n", 0xC);
    }
}

// Helper: find substring in string
char *strstr_simple(char *haystack, char *needle) {
    if (*needle == '\0') return haystack;
    
    for (; *haystack != '\0'; haystack++) {
        char *h = haystack;
        char *n = needle;
        
        while (*h != '\0' && *n != '\0' && *h == *n) {
            h++;
            n++;
        }
        
        if (*n == '\0') return haystack;
    }
    return (char *)0;
}

void cmd_grep(char *pattern, char *filename) {
    if (pattern[0] == '\0') {
        print("grep: missing pattern\n", 0xC);
        return;
    }
    
    char *content;
    uint32_t content_len;
    struct BlockBuffer buf[BLOCK_COUNT];
    
    if (pipe_buffer_len > 0) {
        // read from pipe
        content = pipe_buffer;
        content_len = pipe_buffer_len;
    } else if (filename[0] != '\0') {
        // read from file
        struct EXT2DriverRequest req = {
            .buf = buf,
            .name = filename,
            .name_len = strlen(filename),
            .parent_inode = shell_state.current_dir_inode,
            .buffer_size = BLOCK_SIZE * BLOCK_COUNT
        };
        
        int8_t retcode;
        syscall_read(&req, &retcode);
        
        if (retcode != 0) {
            print("grep: cannot read file\n", 0xC);
            return;
        }
        
        content = (char *)buf;
        content_len = BLOCK_SIZE * BLOCK_COUNT;
    } else {
        print("grep: no input (use: grep pattern file OR cmd | grep pattern)\n", 0xC);
        return;
    }
    
    // Process line by line
    char line[512];
    uint32_t line_idx = 0;
    
    for (uint32_t i = 0; i <= content_len; i++) {
        if (content[i] == '\n' || content[i] == '\0' || i == content_len) {
            line[line_idx] = '\0';
            
            // Check if pattern exists in line
            if (line_idx > 0 && strstr_simple(line, pattern) != (char *)0) {
                print(line, 0xF);
                print("\n", 0xF);
            }
            
            line_idx = 0;
            if (content[i] == '\0') break;
        } else if (line_idx < 511) {
            line[line_idx++] = content[i];
        }
    }
}

void cmd_kill(char *pid_str) {
    if (pid_str[0] == '\0') {
        print("kill: missing pid\n", 0xC);
        return;
    }
    
    // Simple atoi
    int32_t pid = 0;
    for (int i = 0; pid_str[i] != '\0'; i++) {
        if (pid_str[i] >= '0' && pid_str[i] <= '9') {
            pid = pid * 10 + (pid_str[i] - '0');
        } else {
            print("kill: invalid pid\n", 0xC);
            return;
        }
    }
    
    // Check process name
    struct ProcessInfo pcb;
    bool process_found = false;
    bool is_shell = false;
    bool is_clock = false;
    
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        syscall_get_process_info(i, &pcb);
        if (pcb.is_active && (int32_t)pcb.pid == pid) {
            process_found = true;
            if (strcmp(pcb.name, "shell") == 0) is_shell = true;
            if (strcmp(pcb.name, "clock") == 0) is_clock = true;
            break;
        }
    }
    
    if (!process_found) {
        print("kill: failed (pid not found)\n", 0xC);
        return;
    }
    
    if (is_shell) {
        print("Error: Cannot kill shell\n", 0xC);
        return;
    }
    
    int8_t ret;
    syscall_kill(pid, &ret);
    
    if (ret == 0) {
        print("Process killed\n", 0xA); // Success
        if (is_clock) {
            // Clear clock display at Row 24, Col 71, Len 8
            // 0xF is White
            syscall_puts_at("        ", 8, 0xF, 24, 71);
        }
    } else {
        print("kill: failed\n", 0xC);
    }
}

void cmd_exec(char *filename) {
    if (filename[0] == '\0') {
        print("exec: missing filename\n", 0xC);
        return;
    }
    
    struct EXT2DriverRequest req = {
        .buf = (uint8_t*)   0, // Load at address 0
        .name = filename,
        .name_len = strlen(filename),
        .parent_inode = 2,
        .buffer_size = 0x100000 // Will be set by kernel
    };
    
    int8_t ret;
    syscall_exec(&req, &ret);
    
    if (ret != 0) {
        print("exec: failed rc=", 0xC);
        char buf[4];
        if (ret < 0) {
            print("-", 0xC);
            ret = -ret;
        }
        buf[0] = ret + '0';
        buf[1] = '\n';
        buf[2] = '\0';
        print(buf, 0xC);
    }
}

void cmd_ps() {
    print("PID  State    Name\n", 0xE);
    struct ProcessInfo pcb;
    
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        syscall_get_process_info(i, &pcb);
        
        if (pcb.is_active) {
            char pid_buf[16];
            snprintf(pid_buf, 16, "%d", pcb.pid);
            print(pid_buf, 0xF);
            print("    ", 0xF); // Padding
            
            char *state_str = "UNKNOWN";
            switch (pcb.state) {
                case READY: state_str = "READY  "; break;
                case RUNNING: state_str = "RUNNING"; break;
                case BLOCKED: state_str = "BLOCKED"; break;
                case DESTROYED: state_str = "DESTROY"; break; 
            }
            print(state_str, 0xF);
            print("  ", 0xF);
            
            print(pcb.name, 0xF);
            print("\n", 0xF);
        }
    }
}

void find_recursive(uint32_t dir_inode, char *path, char *target) {
    struct BlockBuffer buf;
    struct EXT2DriverRequest req = {
        .buf = &buf,
        .name = ".",
        .name_len = 1,
        .parent_inode = dir_inode,
        .buffer_size = BLOCK_SIZE,
        .is_directory = true
    };
    
    int8_t retcode;
    syscall_read_directory(&req, &retcode);
    if (retcode != 0) return;
    
    uint32_t offset = 0;
    struct EXT2DirectoryEntry *entry;
    
    while (offset < BLOCK_SIZE) {
        entry = (struct EXT2DirectoryEntry *)(buf.buf + offset);
        if (entry->rec_len == 0 || entry->inode == 0) break;
        
        char *name = (char *)(entry + 1);
        
        // Skip . and ..
        if ((entry->name_len == 1 && name[0] == '.') ||
            (entry->name_len == 2 && name[0] == '.' && name[1] == '.')) {
            offset += entry->rec_len;
            continue;
        }
        
        char name_buf[64];
        for (uint8_t i = 0; i < entry->name_len && i < 63; i++) {
            name_buf[i] = name[i];
        }
        name_buf[entry->name_len] = '\0';
        
        // full pathnya
        char fullpath[256];
        uint32_t path_len = strlen(path);
        
        // curr path
        for (uint32_t i = 0; i < path_len && i < 250; i++) {
            fullpath[i] = path[i];
        }
        fullpath[path_len] = '\0';
        
        // append slash
        if (strcmp(path, "/") != 0) {
            fullpath[path_len] = '/';
            fullpath[path_len + 1] = '\0';
        }
        
        // append name
        strcat(fullpath, name_buf);
        
        if (strcmp(name_buf, target) == 0) {
            print(fullpath, 0xA);
            print("\n", 0xF);
        }
        if (entry->file_type == EXT2_FT_DIR) {
            find_recursive(entry->inode, fullpath, target);
        }
        
        offset += entry->rec_len;
    }
}

void cmd_find(char *target) {
    if (target[0] == '\0') {
        print("find: missing filename\n", 0xC);
        return;
    }
    
    find_recursive(2, "/", target);
}

void cmd_clock() {
    struct ProcessInfo pcb;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        syscall_get_process_info(i, &pcb);
        if (pcb.is_active && strcmp(pcb.name, "clock") == 0) {
            print("Error: Clock is already running\n", 0xC);
            return;
        }
    }
    cmd_exec("clock");
}


// Execute command
void execute_command(struct Command *cmd) {
    if (cmd->cmd[0] == '\0') return;
    
    if (strcmp(cmd->cmd, "ls") == 0) {
        cmd_ls();
    } else if (strcmp(cmd->cmd, "cd") == 0) {
        cmd_cd(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "pwd") == 0) {
        print(shell_state.current_path, 0xE);
        print("\n", 0xF);
    } else if (strcmp(cmd->cmd, "mkdir") == 0) {
        cmd_mkdir(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "cat") == 0) {
        cmd_cat(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "cp") == 0) {
        cmd_cp(cmd->args[0], cmd->args[1]);
    } else if (strcmp(cmd->cmd, "rm") == 0) {
        cmd_rm(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "mv") == 0) {
        cmd_mv(cmd->args[0], cmd->args[1]);
    } else if (strcmp(cmd->cmd, "echo") == 0) {
        cmd_echo(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "touch") == 0) {
        cmd_touch(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "grep") == 0) {
        cmd_grep(cmd->args[0], cmd->args[1]);
    } else if (strcmp(cmd->cmd, "find") == 0) {
        cmd_find(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "exec") == 0) {
        cmd_exec(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "kill") == 0) {
        cmd_kill(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "ps") == 0) {
        cmd_ps();
    } else if (strcmp(cmd->cmd, "clock") == 0) {
        cmd_clock();
    } else if (strcmp(cmd->cmd, "clear") == 0) {
        // Clear screen
        syscall(10,0,0,0);
    } else if (strcmp(cmd->cmd, "help") == 0) {
        print_or_pipe("Available commands:\n", 0xE);
        print_or_pipe("  ls       - list directory\n", 0xF);
        print_or_pipe("  cd       - change directory\n", 0xF);
        print_or_pipe("  pwd      - print working directory\n", 0xF);
        print_or_pipe("  mkdir    - create directory\n", 0xF);
        print_or_pipe("  cat      - display file\n", 0xF);
        print_or_pipe("  cp       - copy file\n", 0xF);
        print_or_pipe("  rm       - remove file/dir\n", 0xF);
        print_or_pipe("  mv       - move/rename\n", 0xF);
        print_or_pipe("  echo     - print text\n", 0xF);
        print_or_pipe("  touch    - create empty file\n", 0xF);
        print_or_pipe("  grep     - search pattern in file\n", 0xF);
        print_or_pipe("  find     - search for files\n", 0xF);
        print_or_pipe("  exec     - execute program\n", 0xF);
        print_or_pipe("  kill     - kill process\n", 0xF);
        print_or_pipe("  ps       - list processes\n", 0xF);
        print_or_pipe("  clock    - show clock\n", 0xF);
        print_or_pipe("  clear    - clear screen\n", 0xF);
        print_or_pipe("  help     - show this help\n", 0xF);
    } else {
        print(cmd->cmd, 0xC);
        print(": command not found\n", 0xC);
    }
}

void read_line(char *buffer, uint32_t max_len) {

    uint32_t pos = 0;
    char c;
    
    while (true) {
        syscall_getchar(&c);
        
        if (c == '\n') {
            buffer[pos] = '\0';
            syscall_putchar('\n', 0xF);
            break;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                syscall_putchar('\b', 0xF);
            }
        } else if (c >= 32 && c < 127) {
            if (pos < max_len - 1) {
                buffer[pos++] = c;
                syscall_putchar(c, 0xF);
            }
        }
    }
}

int main(void) {
    shell_state.current_dir_inode = 2;
    strcpy(shell_state.current_path, "/");
    
    char input_buffer[INPUT_BUFFER_SIZE];
    struct Command cmd;

    syscall_activate_keyboard();

    while (true) {
        char path[512];
        char *user = "root";
        char *at = "@";
        char *host = "MentOS2130";
        char *dollar = "$ ";

        syscall(6, (uint32_t)user, 4, FB_GREEN);
        syscall(6, (uint32_t)at, 1, FB_LIGHT_GRAY);
        syscall(6, (uint32_t)host, 10, FB_BRIGHT_BLUE);
        int pathlen = snprintf(path, sizeof(path), "%s", shell_state.current_path);
        syscall(6, (uint32_t)path, pathlen, FB_BRIGHT_BLUE);
        syscall(6, (uint32_t)dollar, 2, FB_WHITE);
        
        read_line(input_buffer, INPUT_BUFFER_SIZE);
        
        // check pipe
        char *pipe_pos = (char *)0;
        for (uint32_t i = 0; input_buffer[i] != '\0'; i++) {
            if (input_buffer[i] == '|') {
                pipe_pos = &input_buffer[i];
                break;
            }
        }
        
        if (pipe_pos != (char *)0) {
            // split command
            *pipe_pos = '\0';
            char *left_cmd = input_buffer;
            char *right_cmd = pipe_pos + 1;
            
            while (*right_cmd == ' ') right_cmd++;
            pipe_buffer_len = 0;
            pipe_buffer[0] = '\0';
            // exec left
            pipe_mode = true;
            parse_input(left_cmd, &cmd);
            execute_command(&cmd);
            pipe_mode = false;
            // exec right
            parse_input(right_cmd, &cmd);
            execute_command(&cmd);
            // clear pipe
            pipe_buffer_len = 0;
            pipe_buffer[0] = '\0';
        } else {
            // Normal exec
            parse_input(input_buffer, &cmd);
            execute_command(&cmd);
        }
    }
    
    return 0;
}