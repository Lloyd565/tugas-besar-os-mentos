#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

static struct EXT2Superblock superblock;
static struct EXT2BlockGroupDescriptorTable bgdt;

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

/* =================== HELPER FUNCTIONS ============================*/
void commit_metadata(void)
{
    struct BlockBuffer buffer;
    // Update superblock
    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf, &superblock, sizeof(superblock));
    write_blocks(&buffer, 1, 1);
    // Update BGDT
    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf, &bgdt, sizeof(bgdt));
    write_blocks(&buffer, 2, 1);
}

char *get_entry_name(void *entry)
{
    return (char *)((struct EXT2DirectoryEntry *)entry + 1);
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset)
{
    return (struct EXT2DirectoryEntry *)((uint8_t *)ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry)
{
    return (struct EXT2DirectoryEntry *)((uint8_t *)entry + entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len)
{
    uint16_t new_length = 8 + name_len;
    if (new_length % 4 != 0)
        new_length += 4 - (new_length % 4);
    return new_length;
}

uint32_t get_dir_first_child_offset(void *ptr)
{
    uint32_t offset = 0;
    struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)(ptr + offset);
    offset += dot->rec_len;
    struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *)(ptr + offset);
    offset += dotdot->rec_len;
    return offset;
}

/* =================== INODE UTILITIES ============================*/
uint32_t inode_to_bgd(uint32_t inode)
{
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode)
{
    return (inode - 1) % INODES_PER_GROUP;
}

/* =================== INODE OPERATIONS ============================*/
void read_inode(uint32_t inode, struct EXT2Inode *out)
{
    if (inode == 0) return;
    
    uint32_t max_inode = INODES_PER_GROUP * GROUPS_COUNT;
    if (inode > max_inode) return;

    uint32_t bgd_index = inode_to_bgd(inode);
    uint32_t local_index = inode_to_local(inode);

    struct BlockBuffer inode_buff;
    uint32_t block_num = bgdt.table[bgd_index].bg_inode_table + (local_index / INODES_PER_TABLE);
    read_blocks(&inode_buff, block_num, 1);

    struct EXT2Inode *inode_table = (struct EXT2Inode *)inode_buff.buf;
    memcpy(out, &inode_table[local_index % INODES_PER_TABLE], sizeof(struct EXT2Inode));
}

void write_inode(uint32_t inode_idx, const struct EXT2Inode *inode)
{
    uint32_t total_inodes = INODES_PER_GROUP * GROUPS_COUNT;
    if (inode_idx == 0 || inode_idx > total_inodes) return;

    uint32_t bgd_index = inode_to_bgd(inode_idx);
    uint32_t local_index = inode_to_local(inode_idx);

    struct BlockBuffer inode_buff;
    uint32_t block_num = bgdt.table[bgd_index].bg_inode_table + (local_index / INODES_PER_TABLE);
    read_blocks(&inode_buff, block_num, 1);

    struct EXT2Inode *inode_table = (struct EXT2Inode *)inode_buff.buf;
    memcpy(&inode_table[local_index % INODES_PER_TABLE], inode, sizeof(struct EXT2Inode));

    write_blocks(&inode_buff, block_num, 1);
}

/* =================== DIRECTORY INITIALIZATION ============================*/
void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode)
{
    struct BlockBuffer bb;
    memset(&bb, 0, sizeof(bb));

    // Entry 1: "."
    struct EXT2DirectoryEntry self = {
        .inode = inode,
        .rec_len = get_entry_record_len(1),
        .name_len = 1,
        .file_type = EXT2_FT_DIR
    };
    memcpy(bb.buf, &self, sizeof(self));
    memcpy(bb.buf + sizeof(self), ".", 1);

    // Entry 2: ".."
    uint32_t offset = self.rec_len;
    struct EXT2DirectoryEntry parent = {
        .inode = parent_inode,
        .rec_len = BLOCK_SIZE - self.rec_len,
        .name_len = 2,
        .file_type = EXT2_FT_DIR
    };
    memcpy(bb.buf + offset, &parent, sizeof(parent));
    memcpy(bb.buf + offset + sizeof(parent), "..", 2);

    write_blocks(&bb, node->i_block[0], 1);
    node->i_blocks = 1;
    node->i_size = BLOCK_SIZE;
}

/* =================== FILESYSTEM INITIALIZATION ============================*/
bool is_empty_storage(void)
{
    struct BlockBuffer boot_sector;
    read_blocks(&boot_sector, BOOT_SECTOR, 1);
    return memcmp(boot_sector.buf, fs_signature, BLOCK_SIZE) != 0;
}

void create_ext2(void)
{
    struct BlockBuffer buffer;

    // 1. Write filesystem signature to boot sector
    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf, fs_signature, BLOCK_SIZE);
    write_blocks(&buffer, BOOT_SECTOR, 1);

    // 2. Initialize Superblock
    memset(&superblock, 0, sizeof(superblock));
    superblock.s_inodes_count = INODES_PER_GROUP * GROUPS_COUNT;
    superblock.s_blocks_count = BLOCKS_PER_GROUP * GROUPS_COUNT;
    superblock.s_r_blocks_count = 0;
    superblock.s_free_blocks_count = BLOCKS_PER_GROUP - (5 + INODES_TABLE_BLOCK_COUNT + 1);
    superblock.s_free_inodes_count = INODES_PER_GROUP - 1;
    superblock.s_first_data_block = 1;
    superblock.s_first_ino = 2;
    superblock.s_blocks_per_group = BLOCKS_PER_GROUP;
    superblock.s_frags_per_group = BLOCKS_PER_GROUP;
    superblock.s_inodes_per_group = INODES_PER_GROUP;
    superblock.s_magic = EXT2_SUPER_MAGIC;
    superblock.s_prealloc_blocks = 16;
    superblock.s_prealloc_dir_blocks = 16;

    // Write superblock to block 1
    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf, &superblock, sizeof(superblock));
    write_blocks(&buffer, 1, 1);

    // 3. Initialize Block Group Descriptor Table
    memset(&bgdt, 0, sizeof(bgdt));
    bgdt.table[0].bg_block_bitmap = 3;
    bgdt.table[0].bg_inode_bitmap = 4;
    bgdt.table[0].bg_inode_table = 5;
    bgdt.table[0].bg_free_blocks_count = BLOCKS_PER_GROUP - (5 + INODES_TABLE_BLOCK_COUNT + 1);
    bgdt.table[0].bg_free_inodes_count = INODES_PER_GROUP - 1;
    bgdt.table[0].bg_used_dirs_count = 1;

    // Write BGDT to block 2
    memset(&buffer, 0, sizeof(buffer));
    memcpy(buffer.buf, &bgdt, sizeof(bgdt));
    write_blocks(&buffer, 2, 1);

    // 4. Initialize Block Bitmap (block 3)
    memset(&buffer, 0, sizeof(buffer));
    buffer.buf[0] = 0xFF; // blocks 0-7
    buffer.buf[1] = 0xFF; // blocks 8-15
    buffer.buf[2] = 0x7F; // blocks 16-22
    write_blocks(&buffer, 3, 1);

    // 5. Initialize Inode Bitmap (block 4)
    memset(&buffer, 0, sizeof(buffer));
    buffer.buf[0] = 0x02; // Inode 2 used
    write_blocks(&buffer, 4, 1);

    // 6. Initialize Inode Table
    memset(&buffer, 0, sizeof(buffer));
    struct EXT2Inode *inode_table = (struct EXT2Inode *)buffer.buf;

    // Root directory is inode 2 (index 1)
    inode_table[1].i_mode = EXT2_S_IFDIR | 0755;
    inode_table[1].i_size = BLOCK_SIZE;
    inode_table[1].i_blocks = BLOCK_SIZE / 512;
    inode_table[1].i_block[0] = bgdt.table[0].bg_inode_table + INODES_TABLE_BLOCK_COUNT;

    for (int i = 1; i < 15; i++) {
        inode_table[1].i_block[i] = 0;
    }

    write_blocks(&buffer, bgdt.table[0].bg_inode_table, 1);

    // Clear remaining inode table blocks
    if (INODES_TABLE_BLOCK_COUNT > 1) {
        memset(&buffer, 0, sizeof(buffer));
        for (uint32_t i = 1; i < INODES_TABLE_BLOCK_COUNT; i++) {
            write_blocks(&buffer, bgdt.table[0].bg_inode_table + i, 1);
        }
    }

    // 7. Initialize root directory content
    memset(&buffer, 0, sizeof(buffer));
    read_blocks(&buffer, bgdt.table[0].bg_inode_table, 1);
    inode_table = (struct EXT2Inode *)buffer.buf;
    struct EXT2Inode *root_node = &inode_table[1];
    init_directory_table(root_node, 2, 2);
    write_blocks(&buffer, bgdt.table[0].bg_inode_table, 1);

    // 8. Clear data blocks
    memset(&buffer, 0, sizeof(buffer));
    uint32_t first_data_block = bgdt.table[0].bg_inode_table + INODES_TABLE_BLOCK_COUNT;
    uint32_t last_block = BLOCKS_PER_GROUP;

    for (uint32_t i = first_data_block; i < last_block; i++) {
        write_blocks(&buffer, i, 1);
    }

    commit_metadata();
}

void initialize_filesystem_ext2(void)
{
    if (is_empty_storage()) {
        create_ext2();
    }

    // Read superblock
    struct BlockBuffer bb;
    memset(&bb, 0, sizeof(bb));
    read_blocks(&bb, 1, 1);
    memcpy(&superblock, bb.buf, sizeof(superblock));

    // Read BGDT
    memset(&bb, 0, sizeof(bb));
    read_blocks(&bb, 2, 1);
    memcpy(&bgdt, bb.buf, sizeof(bgdt));
}

/* =================== DIRECTORY UTILITIES ============================*/
bool is_directory_empty(uint32_t inode)
{
    struct EXT2Inode node_data;
    read_inode(inode, &node_data);
    struct EXT2Inode *node = &node_data;

    struct BlockBuffer dbuff;
    read_blocks(&dbuff, node->i_block[0], 1);

    struct EXT2DirectoryEntry *first_entry = (struct EXT2DirectoryEntry *)dbuff.buf;
    uint32_t offset = first_entry->rec_len;

    struct EXT2DirectoryEntry *second_entry = (struct EXT2DirectoryEntry *)(dbuff.buf + offset);
    offset += second_entry->rec_len;

    struct EXT2DirectoryEntry *third_entry = (struct EXT2DirectoryEntry *)(dbuff.buf + offset);
    return third_entry->inode == 0;
}

/* =================== BITMAP OPERATIONS ============================*/
bool is_block_used(uint32_t block_number)
{
    struct BlockBuffer bitmap_buff;
    read_blocks(&bitmap_buff, bgdt.table[0].bg_block_bitmap, 1);

    uint32_t byte_index = block_number / 8;
    uint32_t bit_index = block_number % 8;

    return (bitmap_buff.buf[byte_index] & (1 << bit_index)) != 0;
}

void set_block_used(uint32_t block_number, bool used)
{
    struct BlockBuffer bitmap_buff;
    read_blocks(&bitmap_buff, bgdt.table[0].bg_block_bitmap, 1);

    uint32_t byte_index = block_number / 8;
    uint32_t bit_index = block_number % 8;

    if (used) {
        bitmap_buff.buf[byte_index] |= (1 << bit_index);
        bgdt.table[0].bg_free_blocks_count--;
    } else {
        bitmap_buff.buf[byte_index] &= ~(1 << bit_index);
        bgdt.table[0].bg_free_blocks_count++;
    }

    write_blocks(&bitmap_buff, bgdt.table[0].bg_block_bitmap, 1);
}

bool is_inode_used(uint32_t inode)
{
    struct BlockBuffer bitmap_buff;
    read_blocks(&bitmap_buff, bgdt.table[0].bg_inode_bitmap, 1);

    uint32_t byte_index = (inode - 1) / 8;
    uint32_t bit_index = (inode - 1) % 8;

    return (bitmap_buff.buf[byte_index] & (1 << bit_index)) != 0;
}

void set_inode_used(uint32_t inode, bool used)
{
    struct BlockBuffer bitmap_buff;
    read_blocks(&bitmap_buff, bgdt.table[0].bg_inode_bitmap, 1);

    uint32_t byte_index = (inode - 1) / 8;
    uint32_t bit_index = (inode - 1) % 8;

    if (used) {
        bitmap_buff.buf[byte_index] |= (1 << bit_index);
        bgdt.table[0].bg_free_inodes_count--;
        superblock.s_free_inodes_count--;
    } else {
        bitmap_buff.buf[byte_index] &= ~(1 << bit_index);
        bgdt.table[0].bg_free_inodes_count++;
        superblock.s_free_inodes_count++;
    }

    write_blocks(&bitmap_buff, bgdt.table[0].bg_inode_bitmap, 1);
}

uint32_t allocate_block(void)
{
    uint32_t first_data_block = bgdt.table[0].bg_inode_table + INODES_TABLE_BLOCK_COUNT;

    for (uint32_t i = first_data_block; i < BLOCKS_PER_GROUP; i++) {
        if (!is_block_used(i)) {
            set_block_used(i, true);
            return i;
        }
    }
    return 0;
}

uint32_t allocate_node(void)
{
    for (uint32_t i = 1; i <= INODES_PER_GROUP; i++) {
        if (!is_inode_used(i)) {
            set_inode_used(i, true);
            return i;
        }
    }
    return 0;
}

/* =================== DIRECTORY ENTRY OPERATIONS ============================*/
struct EXT2DirectoryEntry *find_entry_in_dir(uint32_t dir_inode, char *name, uint8_t name_len)
{
    struct EXT2Inode dir_node;
    read_inode(dir_inode, &dir_node);

    if (!(dir_node.i_mode & EXT2_S_IFDIR)) {
        return (struct EXT2DirectoryEntry *)0;
    }

    struct BlockBuffer dir_buff;
    read_blocks(&dir_buff, dir_node.i_block[0], 1);

    uint32_t offset = 0;
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(dir_buff.buf + offset);

        if (entry->rec_len == 0) break;

        if (entry->inode != 0 && entry->name_len == name_len) {
            char *entry_name = get_entry_name(entry);
            if (memcmp(entry_name, name, name_len) == 0) {
                return entry;
            }
        }

        offset += entry->rec_len;
    }

    return (struct EXT2DirectoryEntry *)0;
}

/* =================== READ OPERATIONS ============================*/
int8_t read(struct EXT2DriverRequest request)
{
    struct EXT2Inode parent_node;
    read_inode(request.parent_inode, &parent_node);

    if (!(parent_node.i_mode & EXT2_S_IFDIR)) {
        return 4;
    }

    struct EXT2DirectoryEntry *entry = find_entry_in_dir(request.parent_inode, request.name, request.name_len);

    if (entry == (struct EXT2DirectoryEntry *)0) {
        return 3;
    }

    if (entry->file_type != EXT2_FT_REG_FILE) {
        return 1;
    }

    struct EXT2Inode file_node;
    read_inode(entry->inode, &file_node);

    if (request.buffer_size < file_node.i_size) {
        return 2;
    }

    uint32_t bytes_read = 0;
    uint32_t blocks_to_read = (file_node.i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (uint32_t i = 0; i < blocks_to_read && i < 12; i++) {
        if (file_node.i_block[i] == 0) break;

        struct BlockBuffer block_buff;
        read_blocks(&block_buff, file_node.i_block[i], 1);

        uint32_t bytes_to_copy = BLOCK_SIZE;
        if (bytes_read + bytes_to_copy > file_node.i_size) {
            bytes_to_copy = file_node.i_size - bytes_read;
        }

        memcpy((uint8_t *)request.buf + bytes_read, block_buff.buf, bytes_to_copy);
        bytes_read += bytes_to_copy;
    }

    return 0;
}
int8_t read_directory(struct EXT2DriverRequest *request)
{
    struct EXT2Inode parent_node;
    read_inode(request->parent_inode, &parent_node);

    if (!(parent_node.i_mode & EXT2_S_IFDIR)) {
        return 3;
    }

    // If name is empty or ".", read the parent_inode itself
    if (request->name_len == 0 || 
        (request->name_len == 1 && request->name[0] == '.')) {
        struct BlockBuffer dir_buff;
        read_blocks(&dir_buff, parent_node.i_block[0], 1);
        memcpy(request->buf, dir_buff.buf, BLOCK_SIZE);
        return 0;
    }

    struct EXT2DirectoryEntry *entry = find_entry_in_dir(request->parent_inode, request->name, request->name_len);

    if (entry == (struct EXT2DirectoryEntry *)0) {
        return 2;
    }

    if (entry->file_type != EXT2_FT_DIR) {
        return 1;
    }

    struct EXT2Inode dir_node;
    read_inode(entry->inode, &dir_node);

    struct BlockBuffer dir_buff;
    read_blocks(&dir_buff, dir_node.i_block[0], 1);

    memcpy(request->buf, dir_buff.buf, BLOCK_SIZE);

    return 0;
}
/* =================== WRITE OPERATIONS ============================*/
int8_t write(struct EXT2DriverRequest *request)
{
    // 1. Validate parent inode
    struct EXT2Inode parent_node;
    read_inode(request->parent_inode, &parent_node);
    if (!(parent_node.i_mode & EXT2_S_IFDIR)) {
        return 2;
    }

    // 2. Check if entry already exists
    struct EXT2DirectoryEntry *existing = find_entry_in_dir(request->parent_inode, request->name, request->name_len);
    if (existing != (struct EXT2DirectoryEntry *)0) {
        return 1;
    }

    // 3. Allocate new inode
    uint32_t new_inode = allocate_node();
    if (new_inode == 0) {
        return -1;
    }

    struct EXT2Inode new_node;
    memset(&new_node, 0, sizeof(new_node));

    if (request->is_directory) {
        // Create directory
        new_node.i_mode = EXT2_S_IFDIR | 0755;
        new_node.i_size = BLOCK_SIZE;
        new_node.i_blocks = BLOCK_SIZE / 512;

        uint32_t dir_block = allocate_block();
        if (dir_block == 0) {
            set_inode_used(new_inode, false);
            return -1;
        }

        new_node.i_block[0] = dir_block;
        init_directory_table(&new_node, new_inode, request->parent_inode);

        bgdt.table[0].bg_used_dirs_count++;
    } else {
        // Create file
        new_node.i_mode = EXT2_S_IFREG | 0644;
        new_node.i_size = request->buffer_size;

        uint32_t blocks_needed = (request->buffer_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        if (blocks_needed > 12) blocks_needed = 12;
        new_node.i_blocks = blocks_needed * (BLOCK_SIZE / 512);

        uint32_t bytes_written = 0;
        for (uint32_t i = 0; i < blocks_needed; i++) {
            uint32_t block_num = allocate_block();
            if (block_num == 0) {
                for (uint32_t j = 0; j < i; j++)
                    set_block_used(new_node.i_block[j], false);
                set_inode_used(new_inode, false);
                return -1;
            }

            new_node.i_block[i] = block_num;

            struct BlockBuffer write_buff;
            memset(&write_buff, 0, sizeof(write_buff));

            uint32_t bytes_to_write = BLOCK_SIZE;
            if (bytes_written + bytes_to_write > request->buffer_size)
                bytes_to_write = request->buffer_size - bytes_written;

            memcpy(write_buff.buf, (uint8_t *)request->buf + bytes_written, bytes_to_write);
            write_blocks(&write_buff, block_num, 1);
            bytes_written += bytes_to_write;
        }
    }

    // 4. Write new inode
    write_inode(new_inode, &new_node);

    // 5. Add entry to parent directory
    struct BlockBuffer parent_buff;
    read_blocks(&parent_buff, parent_node.i_block[0], 1);

    uint32_t offset = 0;
    struct EXT2DirectoryEntry *last_entry = (struct EXT2DirectoryEntry *)0;

    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(parent_buff.buf + offset);
        if (entry->rec_len == 0) break;
        last_entry = entry;
        offset += entry->rec_len;
    }

    uint16_t new_rec_len = get_entry_record_len(request->name_len);
    if (last_entry != (struct EXT2DirectoryEntry *)0) {
        uint16_t actual_last_len = get_entry_record_len(last_entry->name_len);
        uint16_t available_space = last_entry->rec_len - actual_last_len;
        if (available_space >= new_rec_len) {
            last_entry->rec_len = actual_last_len;
            offset = ((uint8_t *)last_entry - parent_buff.buf) + actual_last_len;
        }
    }

    struct EXT2DirectoryEntry *new_entry = (struct EXT2DirectoryEntry *)(parent_buff.buf + offset);
    new_entry->inode = new_inode;
    new_entry->rec_len = BLOCK_SIZE - offset;
    new_entry->name_len = request->name_len;
    new_entry->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    memcpy(get_entry_name(new_entry), request->name, request->name_len);

    write_blocks(&parent_buff, parent_node.i_block[0], 1);
    commit_metadata();

    return 0;
}

/* =================== DELETE OPERATIONS ============================*/
/* =================== DELETE OPERATIONS ============================*/
int8_t delete(struct EXT2DriverRequest request)
{
    struct EXT2Inode parent_node;
    read_inode(request.parent_inode, &parent_node);

    if (!(parent_node.i_mode & EXT2_S_IFDIR)) {
        return 3;
    }

    struct BlockBuffer parent_buff;
    read_blocks(&parent_buff, parent_node.i_block[0], 1);

    uint32_t offset = 0;
    struct EXT2DirectoryEntry *prev_entry = (struct EXT2DirectoryEntry *)0;
    struct EXT2DirectoryEntry *target_entry = (struct EXT2DirectoryEntry *)0;

    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(parent_buff.buf + offset);

        if (entry->rec_len == 0) break;

        if (entry->inode != 0 && entry->name_len == request.name_len) {
            char *entry_name = get_entry_name(entry);
            if (memcmp(entry_name, request.name, request.name_len) == 0) {
                target_entry = entry;
                break;
            }
        }

        prev_entry = entry;
        offset += entry->rec_len;
    }

    if (target_entry == (struct EXT2DirectoryEntry *)0) {
        return 1;
    }

    struct EXT2Inode target_node;
    read_inode(target_entry->inode, &target_node);

    if (target_node.i_mode & EXT2_S_IFDIR) {
        if (!is_directory_empty(target_entry->inode)) {
            return 2;
        }
        bgdt.table[0].bg_used_dirs_count--;
    }

    // Free all blocks
    for (uint32_t i = 0; i < 12; i++) {
        if (target_node.i_block[i] != 0) {
            set_block_used(target_node.i_block[i], false);
        }
    }

    // Free the inode
    set_inode_used(target_entry->inode, false);

    // Remove entry from directory
    if (prev_entry != (struct EXT2DirectoryEntry *)0) {
        // Not the first entry - merge with previous
        prev_entry->rec_len += target_entry->rec_len;
    } else {
        // First entry - mark as deleted
        target_entry->inode = 0;
    }

    // Write updated directory back
    write_blocks(&parent_buff, parent_node.i_block[0], 1);
    commit_metadata();

    return 0;
}