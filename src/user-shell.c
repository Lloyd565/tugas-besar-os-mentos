#include <stdint.h>           // uint32_t, int8_t, dll
#include <stdbool.h>          // bool, true, false
#include "header/filesystem/ext2.h"  // EXT2 structures
#include "header/stdlib/string.h"    // memset, memcmp
#include "header/text/framebuffer.h"
#include "header/driver/speaker.h"   // speaker_beep functions

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
    syscall(4, (uint32_t)buf, 0, 0);
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

void syscall_speaker_beep(uint16_t frequency, uint32_t duration) {
    syscall(11, (uint32_t)frequency, duration, 0);
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
            syscall_putchar(content[i], 0xF);
        }
        print("\n", 0xF);
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

void cmd_beep() {
    speaker_beep_simple(500);  // 1000Hz, 500ms
}

void cmd_speaker(char *freq_str, char *duration_str) {
    if (!freq_str || !duration_str) {
        print("speaker: missing arguments\n", 0xC);
        return;
    }
    
    // Simple parsing - convert string to uint32_t
    uint32_t freq = 1000;  // default
    uint32_t duration = 200;  // default
    
    // Parse frequency
    if (freq_str[0] != '\0') {
        freq = 0;
        for (int i = 0; freq_str[i] >= '0' && freq_str[i] <= '9'; i++) {
            freq = freq * 10 + (freq_str[i] - '0');
        }
    }
    
    // Parse duration
    if (duration_str[0] != '\0') {
        duration = 0;
        for (int i = 0; duration_str[i] >= '0' && duration_str[i] <= '9'; i++) {
            duration = duration * 10 + (duration_str[i] - '0');
        }
    }
    
    speaker_beep((uint16_t)freq, duration);
}

void cmd_play(char *filename) {
    if (!filename || filename[0] == '\0') {
        print("play: missing filename\n", 0xC);
        return;
    }
    
    struct BlockBuffer buf[BLOCK_COUNT];
    struct EXT2DriverRequest request = {
        .buf = buf,
        .name = filename,
        .name_len = strlen(filename),
        .parent_inode = shell_state.current_dir_inode,
        .buffer_size = BLOCK_SIZE * BLOCK_COUNT
    };
    
    int8_t read_status = 0;
    syscall_read(&request, &read_status);
    
    if (read_status != 0) {
        print("play: file not found or cannot read\n", 0xC);
        return;
    }
    
    // Parse music file format: freq duration (space/newline separated)
    char *content = (char *)buf;
    uint32_t freq = 0;
    uint32_t duration = 0;
    int parsing_freq = 1;
    
    for (uint32_t i = 0; i < BLOCK_SIZE * BLOCK_COUNT && content[i] != '\0'; i++) {
        char c = content[i];
        
        if (c >= '0' && c <= '9') {
            if (parsing_freq) {
                freq = freq * 10 + (c - '0');
            } else {
                duration = duration * 10 + (c - '0');
            }
        } else if (c == ' ' || c == '\t') {
            if (freq > 0 && parsing_freq) {
                parsing_freq = 0;
            }
        } else if (c == '\n' || c == '\r' || c == '#') {
            // End of line or comment
            if (freq > 0 && duration > 0) {
                // Play the note
                speaker_beep((uint16_t)freq, duration);
            }
            
            // Skip rest of line if comment
            if (c == '#') {
                while (i < BLOCK_SIZE * BLOCK_COUNT && content[i] != '\n' && content[i] != '\r') {
                    i++;
                }
            }
            
            freq = 0;
            duration = 0;
            parsing_freq = 1;
        }
    }
    
    print("play: done\n", 0xA);
}

void cmd_touch(char *filename) {
    if (filename[0] == '\0') {
        print("touch: missing operand\n", 0xC);
        return;
    }
    
    struct BlockBuffer buf;
    struct EXT2DriverRequest req = {
        .buf = &buf,
        .name = filename,
        .name_len = strlen(filename),
        .parent_inode = shell_state.current_dir_inode,
        .buffer_size = 0,
        .is_directory = false
    };
    
    int8_t retcode;
    syscall_write(&req, &retcode);
    
    if (retcode == 0) {
        print("File created\n", 0xA);
    } else if (retcode == 1) {
        print("touch: file already exists\n", 0xC);
    } else {
        print("touch: error creating file\n", 0xC);
    }
}

// Execute command
void execute_command(struct Command *cmd) {
    if (cmd->cmd[0] == '\0') {
        return;  // Empty command, silent return
    }
    
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
    } else if (strcmp(cmd->cmd, "clear") == 0) {
        // Clear screen
        syscall(10,0,0,0);
    } else if (strcmp(cmd->cmd, "beep") == 0) {
        cmd_beep();
    } else if (strcmp(cmd->cmd, "speaker") == 0) {
        cmd_speaker(cmd->args[0], cmd->args[1]);
    } else if (strcmp(cmd->cmd, "play") == 0) {
        cmd_play(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "help") == 0) {
        print("Available commands:\n", 0xE);
        print("  ls       - list directory\n", 0xF);
        print("  cd       - change directory\n", 0xF);
        print("  pwd      - print working directory\n", 0xF);
        print("  mkdir    - create directory\n", 0xF);
        print("  cat      - display file\n", 0xF);
        print("  cp       - copy file\n", 0xF);
        print("  rm       - remove file/dir\n", 0xF);
        print("  mv       - move/rename\n", 0xF);
        print("  echo     - print text\n", 0xF);
        print("  touch    - create empty file\n", 0xF);
        print("  clear    - clear screen\n", 0xF);
        print("  beep     - simple beep (1000Hz, 500ms)\n", 0xF);
        print("  speaker  - beep with custom frequency/duration\n", 0xF);
        print("  play     - play music file (freq duration format)\n", 0xF);
        print("  help     - show this help\n", 0xF);
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
    char path[512];  // MOVED OUTSIDE LOOP
    struct Command cmd;

    syscall_activate_keyboard();

    while (true) {
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
        
        parse_input(input_buffer, &cmd);
        execute_command(&cmd);
    }
    
    return 0;
}