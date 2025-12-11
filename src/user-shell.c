#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"
#include "header/driver/speaker.h"   // speaker_beep functions

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
#define CLIPBOARD_SIZE 256

// Clipboard structure for copy/paste
struct Clipboard {
    char content[CLIPBOARD_SIZE];
    uint32_t length;
} clipboard = {
    .content = {0},
    .length = 0
};

// Text selection structure
struct TextSelection {
    uint32_t start_x;  // Start column in framebuffer
    uint32_t start_y;  // Start row in framebuffer
    uint32_t end_x;    // End column
    uint32_t end_y;    // End row
    bool is_active;    // Is selection active
    char selected_text[CLIPBOARD_SIZE];
    uint32_t selected_length;
} text_selection = {
    .start_x = 0,
    .start_y = 0,
    .end_x = 0,
    .end_y = 0,
    .is_active = false,
    .selected_length = 0
};

// Output buffer to track all printed text with coordinates
#define OUTPUT_BUFFER_SIZE 2048
struct OutputLine {
    char text[256];      // Text printed on this line
    uint32_t length;     // Length of text on this line
    uint32_t screen_row; // Which row on screen (0-23)
};

struct OutputBuffer {
    struct OutputLine lines[25];  // 25 rows on screen (0-24)
    uint32_t current_row;         // Current line being written to
    uint32_t current_col;         // Current column position
} output_buffer = {
    .current_row = 0,
    .current_col = 0
};

// Forward declarations
void track_output(char *str, uint32_t len);
void extract_selected_text(uint32_t start_x, uint32_t start_y, uint32_t end_x, uint32_t end_y);

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
    // Track single character output
    track_output(&c, 1);
    syscall(5, (uint32_t)c, color, 0);
}

void syscall_puts(char *str, uint32_t len, uint8_t color) {
    // Track output before putting it
    track_output(str, len);
    syscall(6, (uint32_t)str, len, color);
}

// Helper function to track output in buffer
void track_output(char *str, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        char c = str[i];
        if (c == '\n') {
            output_buffer.current_row++;
            output_buffer.current_col = 0;
            if (output_buffer.current_row >= 25) {
                output_buffer.current_row = 24; // Stay at last row
            }
        } else {
            // Add character to current line if there's space
            if (output_buffer.current_col < 256) {
                output_buffer.lines[output_buffer.current_row].text[output_buffer.current_col] = c;
                output_buffer.lines[output_buffer.current_row].length = output_buffer.current_col + 1;
                output_buffer.current_col++;
            }
        }
    }
}

// Extract text from selection coordinates
void extract_selected_text(uint32_t start_x, uint32_t start_y, uint32_t end_x, uint32_t end_y) {
    // Normalize coordinates (start should be before end)
    if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
        uint32_t tmp_x = start_x;
        uint32_t tmp_y = start_y;
        start_x = end_x;
        start_y = end_y;
        end_x = tmp_x;
        end_y = tmp_y;
    }
    
    uint32_t pos = 0;
    text_selection.is_active = true;
    
    // Framebuffer base address (80x25 text mode)
    uint16_t *framebuffer = (uint16_t *)0xC00B8000;
    
    if (start_y == end_y) {
        // Single line selection
        if (start_y < 25) {
            for (uint32_t x = start_x; x <= end_x && x < 80 && pos < CLIPBOARD_SIZE; x++) {
                uint16_t cell = framebuffer[start_y * 80 + x];
                char c = (char)(cell & 0xFF);  // Lower 8 bits = character
                if (c != 0) {  // Skip null characters
                    text_selection.selected_text[pos++] = c;
                }
            }
        }
    } else {
        // Multi-line selection
        for (uint32_t y = start_y; y <= end_y && y < 25; y++) {
            uint32_t col_start = (y == start_y) ? start_x : 0;
            uint32_t col_end = (y == end_y) ? end_x : 79;
            
            for (uint32_t x = col_start; x <= col_end && x < 80 && pos < CLIPBOARD_SIZE; x++) {
                uint16_t cell = framebuffer[y * 80 + x];
                char c = (char)(cell & 0xFF);  // Lower 8 bits = character
                if (c != 0) {  // Skip null characters
                    text_selection.selected_text[pos++] = c;
                }
            }
            
            // Add newline between lines (except last line)
            if (y < end_y && pos < CLIPBOARD_SIZE) {
                text_selection.selected_text[pos++] = '\n';
            }
        }
    }
    
    text_selection.selected_length = pos;
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

// Speaker syscalls
void syscall_speaker_beep(uint16_t frequency, uint32_t duration) {
    syscall(11, (uint32_t)frequency, duration, 0);
}

bool syscall_is_ctrl_c_pressed(void) {
    bool result = false;
    syscall(12, (uint32_t)&result, 0, 0);
    return result;
}

// Mouse syscalls
void syscall_mouse_init(void) {
    syscall(13, 0, 0, 0);
}

void syscall_mouse_get_state(uint32_t *x, uint32_t *y, uint8_t *buttons) {
    syscall(14, (uint32_t)x, (uint32_t)y, (uint32_t)buttons);
}

bool syscall_mouse_get_click(void) {
    bool result = false;
    syscall(15, (uint32_t)&result, 0, 0);
    return result;
}

void syscall_render_mouse_pointer(uint32_t x, uint32_t y, uint8_t color) {
    syscall(16, x, y, color);
}

bool syscall_is_ctrl_pressed(void) {
    bool result = false;
    syscall(17, (uint32_t)&result, 0, 0);
    return result;
}

bool syscall_is_shift_pressed(void) {
    bool result = false;
    syscall(18, (uint32_t)&result, 0, 0);
    return result;
}

// Get mouse drag state
bool syscall_get_mouse_drag_state(uint32_t *coords) {
    // coords is array: [start_x, start_y, end_x, end_y]
    bool drag_active = false;
    syscall(19, (uint32_t)&drag_active, (uint32_t)coords, 0);
    return drag_active;
}

// Process syscalls (updated numbers to match interrupt.c)
void syscall_kill(int32_t pid, int8_t *retcode) {
    syscall(20, (uint32_t)&pid, (uint32_t)retcode, 0);
}

void syscall_exec(struct EXT2DriverRequest *request, int8_t *retcode) {
    syscall(21, (uint32_t)request, (uint32_t)retcode, 0);
}

void syscall_get_process_info(uint32_t index, struct ProcessInfo *pcb) {
    syscall(22, index, (uint32_t)pcb, 0);
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
        // Check for Ctrl+C
        if (syscall_is_ctrl_c_pressed()) {
            print("\nplay: interrupted\n", 0xE);
            return;
        }
        
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
    
    int8_t ret;
    syscall_kill(pid, &ret);
    
    if (ret == 0) {
        print("Process killed\n", 0xA); // Success
    } else {
        print("kill: failed (pid not found?)\n", 0xC);
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
    cmd_exec("bin/clock");
}


// Execute command
// Command: gui - file manager with mouse support
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
    } else if (strcmp(cmd->cmd, "beep") == 0) {
        cmd_beep();
    } else if (strcmp(cmd->cmd, "speaker") == 0) {
        cmd_speaker(cmd->args[0], cmd->args[1]);
    } else if (strcmp(cmd->cmd, "play") == 0) {
        cmd_play(cmd->args[0]);
    } else if (strcmp(cmd->cmd, "mouse") == 0) {
        if (cmd->args[0][0] == '\0') {
            print("Usage: mouse [on|off]\n", 0xC);
            return;
        }
        if (strcmp(cmd->args[0], "on") == 0) {
            syscall_mouse_init();
            print("Mouse enabled. Move mouse to see pointer on screen.\n", 0x0A);
        } else if (strcmp(cmd->args[0], "off") == 0) {
            print("Mouse disabled.\n", 0x0A);
        } else {
            print("mouse: invalid argument\n", 0xC);
        }
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
        print_or_pipe("  beep     - simple beep (1000Hz, 500ms)\n", 0xF);
        print_or_pipe("  speaker  - beep with custom frequency/duration\n", 0xF);
        print_or_pipe("  play     - play music file (freq duration format)\n", 0xF);
        print_or_pipe("  help     - show this help\n", 0xF);
    } else {
        print(cmd->cmd, 0xC);
        print(": command not found\n", 0xC);
    }
}

// Update text selection from mouse drag
void update_mouse_selection(void) {
    uint32_t coords[4] = {0};
    bool drag_active = syscall_get_mouse_drag_state(coords);
    
    if (drag_active) {
        // Extract and store selected text
        extract_selected_text(coords[0], coords[1], coords[2], coords[3]);
    } else {
        text_selection.is_active = false;
        text_selection.selected_length = 0;
    }
}

void read_line(char *buffer, uint32_t max_len) {

    uint32_t pos = 0;
    char c;
    
    while (true) {
        // Update mouse selection on each iteration (before blocking on getchar)
        update_mouse_selection();
        
        // Check for Ctrl+Shift+C (copy) BEFORE blocking on getchar
        if (syscall_is_ctrl_pressed() && syscall_is_shift_pressed()) {
            // Delay a bit to allow character to come through
            for (volatile uint32_t j = 0; j < 100000; j++);
            continue;  // Don't block, just skip this iteration
        }
        
        syscall_getchar(&c);
        
        // Update mouse selection again after getchar returns
        update_mouse_selection();
        
        // Check for Ctrl+C (ASCII 3)
        if (c == 0x03) {
            syscall_putchar('^', 0xF);
            syscall_putchar('C', 0xF);
            syscall_putchar('\n', 0xF);
            buffer[0] = '\0';
            return;
        }
        
        // Check for Ctrl+Shift+V (paste from clipboard)
        // c akan berisi 'v' jika user tekan Ctrl+Shift+V
        if (syscall_is_ctrl_pressed() && syscall_is_shift_pressed() && (c == 'v' || c == 'V')) {
            // Paste clipboard content
            if (clipboard.length > 0) {
                for (uint32_t i = 0; i < clipboard.length && pos < max_len - 1; i++) {
                    buffer[pos] = clipboard.content[i];
                    syscall_putchar(clipboard.content[i], 0xF);
                    pos++;
                }
            }
            continue;
        }
        
        // Check for Ctrl+Shift+C (copy) - c akan berisi 'c' jika user tekan Ctrl+Shift+C
        if (syscall_is_ctrl_pressed() && syscall_is_shift_pressed() && (c == 'c' || c == 'C')) {
            // Check if there's a mouse selection active
            uint32_t coords[4] = {0};
            bool has_selection = syscall_get_mouse_drag_state(coords);
            
            if (has_selection && text_selection.selected_length > 0) {
                // Copy selected output text to clipboard
                clipboard.length = text_selection.selected_length;
                memcpy(clipboard.content, text_selection.selected_text, text_selection.selected_length);
            } else {
                // Copy current input line to clipboard
                clipboard.length = pos;
                memcpy(clipboard.content, buffer, pos);
            }
            continue;
        }
        
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
        
        // Check if Ctrl+C was pressed (empty buffer) - from mouse branch
        if (input_buffer[0] == '\0') {
            continue;  // Skip command execution and show prompt again
        }
        
        // Check for pipe - from milestone-3-new branch
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
        
        // Update mouse selection after command output is printed - from mouse branch
        // This allows user to select text that was just printed
        update_mouse_selection();
    }
    
    return 0;
}