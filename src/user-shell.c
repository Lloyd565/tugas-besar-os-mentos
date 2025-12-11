#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"

#define BLOCK_COUNT 2560
#define INPUT_BUFFER_SIZE 256
#define MAX_ARGS 8

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

// PATH RESOLUTION HELPERS 

/**
 * path_to_parent_inode - Resolve a path to its parent directory inode and filename.
 * Supports absolute paths (starting with /) and relative paths.
 * Handles "." and ".." navigation.
 * 
 * @param path_input Input path
 * @param parent_inode_out Output: inode of the parent directory
 * @param name_out Output: name of the final component
 * @param name_len_out Output: length of the name
 * @return true if success
 */
static bool path_to_parent_inode(const char *path_input, uint32_t *parent_inode_out,
                                 char *name_out, uint8_t *name_len_out) {
    if (!path_input || !parent_inode_out || !name_out || !name_len_out) return false;
    if (path_input[0] == '\0') return false;
    if (strcmp((char*)path_input, "/") == 0) return false;

    uint32_t inode = (path_input[0] == '/') ? 2 : shell_state.current_dir_inode;
    char path_copy[128];
    uint32_t len = strlen(path_input);
    if (len >= 128) len = 127;
    memcpy(path_copy, path_input, len);
    path_copy[len] = '\0';
    char *token = path_copy;
    if (token[0] == '/') token++;
    char *last_comp = (char *)0;
    uint8_t last_len = 0;
    while (true) {
        char *slash = token;
        while (*slash && *slash != '/') slash++;
        bool last = (*slash == '\0');
        char saved = *slash;
        *slash = '\0';
        if (token[0] == '\0' || (token[0] == '.' && token[1] == '\0')) {
            // nothing
        } 
        // Handle ".."
        else if (token[0] == '.' && token[1] == '.' && token[2] == '\0') {
            // Read current dir to find ".." entry
            struct BlockBuffer bufp;
            struct EXT2DriverRequest reqp = {
                .buf = &bufp,
                .name = ".",
                .name_len = 1,
                .parent_inode = inode,
                .buffer_size = BLOCK_SIZE,
                .is_directory = true
            };
            int8_t retp;
            syscall_read_directory(&reqp, &retp);
            if (retp != 0) return false;

            // First entry is ".", second is ".."
            struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)bufp.buf;
            struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *)((uint8_t *)dot + dot->rec_len);
            inode = dotdot->inode;
        } 
        else {
            if (last) {
                // This is the final component (the name we want)
                last_comp = token;
                last_len = (uint8_t)strlen(token);
            } else {
                // Intermediate directory - navigate into it
                struct BlockBuffer dirbuf;
                struct EXT2DriverRequest req = {
                    .buf = &dirbuf,
                    .name = token,
                    .name_len = strlen(token),
                    .parent_inode = inode,
                    .buffer_size = BLOCK_SIZE,
                    .is_directory = true
                };
                int8_t ret;
                syscall_read_directory(&req, &ret);
                if (ret != 0) return false;

                // Get inode from "." entry
                struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)dirbuf.buf;
                inode = dot->inode;
            }
        }

        *slash = saved;
        if (last) break;
        token = slash + 1;
    }

    if (last_comp == (char *)0 || last_len == 0 || last_len > 31) return false;

    *parent_inode_out = inode;
    *name_len_out = last_len;
    memcpy(name_out, last_comp, last_len);
    name_out[last_len] = '\0';
    return true;
}

/**
 * path_to_dir_inode - Resolve a directory path to its inode.
 * Handles ".", "..", and paths like "../.." or "foo/.."
 * 
 * @param path_input Input path
 * @param inode_out Output: inode of the directory
 * @return true if success
 */
static bool path_to_dir_inode(const char *path_input, uint32_t *inode_out) {
    if (!path_input || !inode_out) return false;
    
    if (strcmp((char*)path_input, "/") == 0) {
        *inode_out = 2;  // root inode
        return true;
    }
    if (strcmp((char*)path_input, ".") == 0) {
        *inode_out = shell_state.current_dir_inode;
        return true;
    }
    uint32_t inode = (path_input[0] == '/') ? 2 : shell_state.current_dir_inode;
    char path_copy[128];
    uint32_t len = strlen(path_input);
    if (len >= 128) len = 127;
    memcpy(path_copy, path_input, len);
    path_copy[len] = '\0';    
    char *token = path_copy;
    if (token[0] == '/') token++;
    while (*token) {
        char *slash = token;
        while (*slash && *slash != '/') slash++;
        char saved = *slash;
        *slash = '\0';
        if (token[0] == '\0' || (token[0] == '.' && token[1] == '\0')) {
            // nothing
        }
        // Handle ".."
        else if (token[0] == '.' && token[1] == '.' && token[2] == '\0') {
            struct BlockBuffer bufp;
            struct EXT2DriverRequest reqp = {
                .buf = &bufp,
                .name = ".",
                .name_len = 1,
                .parent_inode = inode,
                .buffer_size = BLOCK_SIZE,
                .is_directory = true
            };
            int8_t retp;
            syscall_read_directory(&reqp, &retp);
            if (retp != 0) return false;
            
            struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)bufp.buf;
            struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *)((uint8_t *)dot + dot->rec_len);
            inode = dotdot->inode;
        }
        else {
            struct BlockBuffer dirbuf;
            struct EXT2DriverRequest req = {
                .buf = &dirbuf,
                .name = token,
                .name_len = strlen(token),
                .parent_inode = inode,
                .buffer_size = BLOCK_SIZE,
                .is_directory = true
            };
            int8_t ret;
            syscall_read_directory(&req, &ret);
            if (ret != 0) return false;
            
            struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)dirbuf.buf;
            inode = dot->inode;
        }
        
        *slash = saved;
        if (saved == '\0') break;
        token = slash + 1;
    }
    
    *inode_out = inode;
    return true;
}

// =================== COMMAND IMPLEMENTATIONS ===================

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
    char new_path[256];
    memset(new_path, 0, 256);
    
    struct EXT2DriverRequest path_req = {
        .buf = shell_state.current_path,
        .name = dirname,
        .name_len = strlen(dirname)
    };
    
    retcode = -1;
    syscall_get_resolved_path(&path_req, new_path, &retcode);
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

// Command: cp - copy file (returns true on success)
// Supports path resolution: cp ../file dest, cp /abs/path dest
bool cmd_cp(char *src, char *dest, bool silent) {
    if (src[0] == '\0' || dest[0] == '\0') {
        if (!silent) print("cp: missing operand\n", 0xC);
        return false;
    }
    
    // Resolve source path
    uint32_t src_parent;
    char src_name[32];
    uint8_t src_name_len;
    
    if (!path_to_parent_inode(src, &src_parent, src_name, &src_name_len)) {
        if (!silent) print("cp: source not found\n", 0xC);
        return false;
    }
    
    // Read source file
    struct BlockBuffer buf[BLOCK_COUNT];
    struct EXT2DriverRequest read_req = {
        .buf = buf,
        .name = src_name,
        .name_len = src_name_len,
        .parent_inode = src_parent,
        .buffer_size = BLOCK_SIZE * BLOCK_COUNT
    };
    
    int8_t retcode;
    syscall_read(&read_req, &retcode);
    
    if (retcode != 0) {
        if (!silent) print("cp: cannot read source file\n", 0xC);
        return false;
    }
    
    // Resolve destination path
    uint32_t dest_parent;
    char dest_name[32];
    uint8_t dest_name_len;
    
    // Check if dest is a directory - use source filename
    uint32_t dest_dir_inode;
    if (path_to_dir_inode(dest, &dest_dir_inode)) {
        // dest is a directory, put file there with same name
        dest_parent = dest_dir_inode;
        memcpy(dest_name, src_name, src_name_len);
        dest_name[src_name_len] = '\0';
        dest_name_len = src_name_len;
    } else {
        // dest is a path to a file
        if (!path_to_parent_inode(dest, &dest_parent, dest_name, &dest_name_len)) {
            if (!silent) print("cp: invalid destination\n", 0xC);
            return false;
        }
    }
    
    // Write to destination
    struct EXT2DriverRequest write_req = {
        .buf = buf,
        .name = dest_name,
        .name_len = dest_name_len,
        .parent_inode = dest_parent,
        .buffer_size = BLOCK_SIZE * BLOCK_COUNT,
        .is_directory = false
    };
    
    syscall_write(&write_req, &retcode);
    
    // If file already exists (retcode == 1), delete it first 
    if (retcode == 1) {
        struct EXT2DriverRequest del_req = {
            .name = dest_name,
            .name_len = dest_name_len,
            .parent_inode = dest_parent,
            .is_directory = false
        };
        int8_t del_retcode;
        syscall_delete(&del_req, &del_retcode);
        
        // Retry write after deletion
        syscall_write(&write_req, &retcode);
    }
    
    if (retcode == 0) {
        if (!silent) print("File copied\n", 0xA);
        return true;
    } else {
        if (!silent) print("cp: error copying file\n", 0xC);
        return false;
    }
}

// Command: rm - remove file
// Only supports deleting files, not directories
void cmd_rm(char *arg1, char *arg2) {
    (void)arg2;  // unused, kept for API compatibility
    
    if (arg1[0] == '\0') {
        print("rm: missing operand\n", 0xC);
        return;
    }
    
    // Resolve path
    uint32_t parent_inode;
    char name[32];
    uint8_t name_len;
    
    if (!path_to_parent_inode(arg1, &parent_inode, name, &name_len)) {
        print("rm: not found\n", 0xC);
        return;
    }
    
    struct EXT2DriverRequest req = {
        .name = name,
        .name_len = name_len,
        .parent_inode = parent_inode,
        .is_directory = false
    };
    
    int8_t retcode;
    syscall_delete(&req, &retcode);
    
    if (retcode == 0) {
        print("Removed\n", 0xA);
    } else if (retcode == 1) {
        print("rm: not found\n", 0xC);
    } else {
        print("rm: error (is it a directory?)\n", 0xC);
    }
}

// Command: mv - move/rename file
// Supports: mv src dest, mv ../file dest, mv file dir/
void cmd_mv(char *src, char *dest) {
    if (src[0] == '\0' || dest[0] == '\0') {
        print("mv: missing operand\n", 0xC);
        return;
    }
    
    // Copy dengan mode silent (true) karena kita akan handle pesan sendiri
    bool copy_success = cmd_cp(src, dest, true);
    
    if (copy_success) {
        // Resolve source for deletion
        uint32_t src_parent;
        char src_name[32];
        uint8_t src_name_len;
        
        if (path_to_parent_inode(src, &src_parent, src_name, &src_name_len)) {
            struct EXT2DriverRequest del_req = {
                .name = src_name,
                .name_len = src_name_len,
                .parent_inode = src_parent
            };
            int8_t retcode;
            syscall_delete(&del_req, &retcode);
        }
        
        print("mv: ", 0xA);
        print(src, 0xA);
        print(" -> ", 0xA);
        print(dest, 0xA);
        print(" success\n", 0xA);
    } else {
        print("mv: failed to move ", 0xC);
        print(src, 0xC);
        print("\n", 0xC);
    }
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
        cmd_cp(cmd->args[0], cmd->args[1], false);
    } else if (strcmp(cmd->cmd, "rm") == 0) {
        cmd_rm(cmd->args[0], cmd->args[1]);
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
    } else if (strcmp(cmd->cmd, "badapple") == 0) {
        #define SIZE 1261824
        #define FRAME_WIDTH 64
        #define FRAME_HEIGHT 24
        #define BYTES_PER_FRAME (FRAME_WIDTH * FRAME_HEIGHT / 8)

        struct BlockBuffer buf[BLOCK_COUNT];
        struct EXT2DriverRequest request = {
            .buf = buf,
            .name = "badapplebit",
            .parent_inode = shell_state.current_dir_inode,
            .buffer_size = BLOCK_SIZE * BLOCK_COUNT,
            .name_len = 11,
            .is_directory = false
        };

        int8_t retcode;
        syscall_read(&request, &retcode);

        if (retcode != 0) {
            print("badapple: failed to load badapplebit file\n", 0xC);
        } else {
            char *buffer = (char *)buf;
            char frame[FRAME_HEIGHT * FRAME_WIDTH] = {0};
            uint32_t num_frames_in_buffer = SIZE / BYTES_PER_FRAME;

            syscall(10, 0, 0, 0); // Clear screen before playing

            for (uint32_t frame_idx = 0; frame_idx < num_frames_in_buffer; frame_idx++) {
                uint32_t current_frame_data_offset = frame_idx * BYTES_PER_FRAME;

                // Convert packed binary to frame of chars
                for (uint32_t j = 0; j < BYTES_PER_FRAME; j++) {
                    char current_packed_byte = buffer[current_frame_data_offset + j];
                    for (uint32_t k = 0; k < 8; k++) {
                        uint32_t char_array_pos = j * 8 + k;
                        if (char_array_pos < (FRAME_HEIGHT * FRAME_WIDTH)) {
                            frame[char_array_pos] = (current_packed_byte & (1 << (7 - k))) ? '#' : ' ';
                        }
                    }
                }

                // Use syscall to draw the frame
                syscall(17, (uint32_t)frame, FRAME_WIDTH, FRAME_HEIGHT);

                // Sleep between frames (200ms untuk delay lebih lama)
                syscall(9, 1000, 0, 0);
            }
            syscall(10, 0, 0, 0); // Clear screen after playing
        }
        #undef SIZE
        #undef FRAME_WIDTH
        #undef FRAME_HEIGHT
        #undef BYTES_PER_FRAME
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
        print_or_pipe("  clear    - clear screen\n", 0xF);
        print_or_pipe("  badapple - play bad apple animation\n", 0xF);
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