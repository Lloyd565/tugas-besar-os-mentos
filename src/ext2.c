#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

/**
 *  REGULAR function
 */

/**
 * get the name of the entry
 * @param entry the directory entry
 * @return the name of the entry
 */

char *get_entry_name(void *entry) {
    struct EXT2DirectoryEntry *dir_entry = (struct EXT2DirectoryEntry *) entry;
    static char buffer[256]; // buffer statis untuk menyimpan nama (maks 255 karakter)
    
    memcpy(buffer, ((uint8_t *)dir_entry) + sizeof(struct EXT2DirectoryEntry), dir_entry->name_len); //nama file dimulai setelah struct EXT2DirectoryEntry
    buffer[dir_entry->name_len] = '\0'; // tambahkan null terminator
    
    return buffer;
}
/**
 * get the directory entry from the buffer
 * @param ptr the buffer that contains the directory table
 * @param offset the offset of the entry 
 * @return the directory entry
 */
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset){
    return (struct EXT2DirectoryEntry *) ((uint8_t *)ptr + offset);
}

/**
 * get the next directory entry from the current entry
 * @param entry the current entry
 * @return the next directory entry
 */
struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry){
    return (struct EXT2DirectoryEntry *) ((uint8_t *)entry + entry->rec_len);
}

/**
 * get the record length of the entry
 * @param name_len the length of the name of the entry
 * @return the record length of the entry
 */
uint16_t get_entry_record_len(uint8_t name_len){
    // di EXT2, setiap entry harus align ke kelipatan 4 byte;
    // uint32_t = 4 byte, uint16_t = 2 byte, unit8_t * 2 = 2 byte, = 8 byte
    uint16_t len = 8 + name_len;
    if (len%4 != 0){
        len += 4 - (len % 4);
    }
    return len;
}

/**
 * get the offset of the first child of the directory
 * @param ptr the buffer that contains the directory table
 * @return the offset of the first child of the directory
 */
uint32_t get_dir_first_child_offset(void *ptr){
    struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)ptr;
    //skip '.' yang nunjuk ke dirinya sendiri
    entry = get_next_directory_entry(entry);

    //skip '..' yang nunjuk ke parentnya
    entry = get_next_directory_entry(entry);

    //entry menunjuk ke first child
    return (uint32_t)((uint8_t *)entry - (uint8_t*)ptr); //kurangin alamatnya
}


/* =================== MAIN FUNCTION OF EXT32 FILESYSTEM ============================*/

/**
 * @brief get bgd index from inode, inode will starts at index 1
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return bgd index (0 to GROUP_COUNT - 1)
 */
uint32_t inode_to_bgd(uint32_t inode){
    return (inode - 1) / INODES_PER_GROUP;
}

/**
 * @brief get inode local index in the corrresponding bgd
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return local index
 */
uint32_t inode_to_local(uint32_t inode){
    return (inode - 1) % INODES_PER_GROUP;
}

/**
 * @brief create a new directory using given node
 * first item of directory table is its node location (name will be .)
 * second item of directory is its parent location (name will be ..)
 * @param node pointer of inode
 * @param inode inode that already allocated
 * @param parent_inode inode of parent directory (if root directory, the parent is itself)
 */
void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    struct BlockBuffer block_buf;                      // bikin buffer 1 blok
    memset(block_buf.buf, 0, BLOCK_SIZE);              // kosongkan isi blok (isi awal 0 semua)

    // Entry pertama: "."
    struct EXT2DirectoryEntry *dir_entry = (struct EXT2DirectoryEntry *) block_buf.buf;
    dir_entry->inode = inode;                          // inode dari direktori ini sendiri
    dir_entry->name_len = 1;                           // nama "." panjangnya 1
    dir_entry->rec_len = get_entry_record_len(dir_entry->name_len); // panjang record dibuletin ke 4-byte alignment
    memcpy(((uint8_t *)dir_entry) + sizeof(struct EXT2DirectoryEntry), ".", dir_entry->name_len);

    // Entry kedua: ".."
    struct EXT2DirectoryEntry *parent_entry = get_next_directory_entry(dir_entry);
    parent_entry->inode = parent_inode;                // inode direktori induk
    parent_entry->name_len = 2;                        // nama ".." panjangnya 2
    parent_entry->rec_len = BLOCK_SIZE - ((uint8_t *)parent_entry - block_buf.buf); // sisa ruang dalam 1 blok
    memcpy(((uint8_t *)parent_entry) + sizeof(struct EXT2DirectoryEntry), "..", parent_entry->name_len);

    // Sekarang tulis isi blok ke disk
    write_blocks(&block_buf, node->i_block[0], 1);
}
/**
 * @brief check whether filesystem signature is missing or not in boot sector
 *
 * @return true if memcmp(boot_sector, fs_signature) returning inequality
 */
bool is_empty_storage(void){
    struct BlockBuffer boot_sector; 
    read_blocks(&boot_sector, BOOT_SECTOR, 1);
    return memcmp(boot_sector.buf, fs_signature, BLOCK_SIZE) != 0;
}

/**
 * @brief create a new EXT2 filesystem. Will write fs_signature into boot sector,
 * initialize super block, bgd table, block and inode bitmap, and create root directory
 */
void create_ext2(void) {
    struct BlockBuffer buffer;
    memset(&buffer, 0, sizeof(buffer)); // isi semua buffer dengan 0

    // Tulis filesystem signature di boot sector (tanda storage sudah diformat EXT2)
    memcpy(buffer.buf, fs_signature, BLOCK_SIZE);
    write_blocks(&buffer, BOOT_SECTOR, 1);

    struct EXT2BlockGroupDescriptorTable BGDT;
    memset(&BGDT, 0, sizeof(BGDT));
    BGDT.table[0].bg_block_bitmap  = BLOCK_BITMAP_BLOCK;
    BGDT.table[0].bg_inode_bitmap  = INODE_BITMAP_BLOCK;
    BGDT.table[0].bg_inode_table   = INODE_TABLE_BLOCK;
    BGDT.table[0].bg_free_blocks_count = BLOCKS_TOTAL - DATA_BLOCK_START;
    BGDT.table[0].bg_free_inodes_count = INODES_TOTAL - 1; // root inode terpakai
    BGDT.table[0].bg_used_dirs_count   = 1;

    struct EXT2Superblock superblock;
    memset(&superblock, 0, sizeof(superblock));
    superblock.s_inodes_count        = INODES_TOTAL;
    superblock.s_blocks_count        = BLOCKS_TOTAL;
    superblock.s_r_blocks_count      = 0; // Tidak ada reserved block
    superblock.s_free_blocks_count   = BGDT.table[0].bg_free_blocks_count;
    superblock.s_free_inodes_count   = BGDT.table[0].bg_free_inodes_count;
    superblock.s_first_data_block    = 1;  // Superblock ada di block 1
    superblock.s_first_ino           = 2;  // Root inode
    superblock.s_blocks_per_group    = BLOCKS_PER_GROUP;
    superblock.s_frags_per_group     = BLOCKS_PER_GROUP;
    superblock.s_inodes_per_group    = INODES_PER_GROUP;
    superblock.s_magic               = EXT2_SUPER_MAGIC;
    superblock.s_prealloc_blocks     = 0;
    superblock.s_prealloc_dir_blocks = 0;

    // Tulis superblock & BGDT ke disk
    write_blocks(&superblock, SUPERBLOCK_BLOCK, 1);
    write_blocks(&BGDT, BGDT_BLOCK, 1);


    struct BlockBuffer bitmap;
    memset(&bitmap, 0, sizeof(bitmap));

    // tandai blok sistem (0..DATA_BLOCK_START-1) sudah terpakai
    for (uint32_t i = 0; i < DATA_BLOCK_START; i++)
        bitmap.buf[i / 8] |= (1 << (i % 8));

    write_blocks(&bitmap, BLOCK_BITMAP_BLOCK, 1);

    memset(&bitmap, 0, sizeof(bitmap));
    bitmap.buf[0] |= (1 << 1); // inode #2 (root dir) terpakai
    write_blocks(&bitmap, INODE_BITMAP_BLOCK, 1);


    struct EXT2InodeTable inode_table;
    memset(&inode_table, 0, sizeof(inode_table));

    // inode #2 = root directory
    inode_table.table[1].i_mode   = EXT2_FT_DIR;
    inode_table.table[1].i_size   = BLOCK_SIZE;
    inode_table.table[1].i_blocks = 1;
    inode_table.table[1].i_block[0] = DATA_BLOCK_START;

    write_blocks(&inode_table, INODE_TABLE_BLOCK, INODE_TABLE_BLOCK_COUNT);


    struct BlockBuffer root_dir;
    memset(&root_dir, 0, sizeof(root_dir));

    // Entry untuk "."
    struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)root_dir.buf;
    dot->inode = 2;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->rec_len = 12; // ukuran minimum dan align ke 4 byte
    memcpy(((uint8_t *)dot) + sizeof(struct EXT2DirectoryEntry), ".", 1);

    // Entry untuk ".."
    struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *)((uint8_t *)dot + dot->rec_len);
    dotdot->inode = 2; // parent = self (root)
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->rec_len = BLOCK_SIZE - dot->rec_len; // sisa block
    memcpy(((uint8_t *)dotdot) + sizeof(struct EXT2DirectoryEntry), "..", 2);

    // Tulis root directory ke data block
    write_blocks(&root_dir, DATA_BLOCK_START, 1);
}

/**
 * @brief Initialize file system driver state, if is_empty_storage() then create_ext2()
 * Else, read and cache super block (located at block 1) and bgd table (located at block 2) into state
 */
void initialize_filesystem_ext2(void) {
    if (is_empty_storage()) {
        create_ext2();
    } else {
        struct EXT2Superblock superblock;
        struct EXT2BlockGroupDescriptorTable bgdt;

        read_blocks(&superblock, SUPERBLOCK_BLOCK, 1);
        read_blocks(&bgdt, BGDT_BLOCK, 1);

        if (superblock.s_magic != EXT2_SUPER_MAGIC) {
            // invalid, tapi gak print apa-apa
        }
    }
}

/**
 * @brief check whether a directory table has children or not
 * @param inode of a directory table
 * @return true if first_child_entry->inode = 0
 */
bool is_directory_empty(uint32_t inode_block_index) {
    struct BlockBuffer block;
    read_blocks(&block, inode_block_index, 1);

    uint32_t offset = 0;
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(block.buf + offset);
        char *name_ptr = ((char *)entry) + sizeof(struct EXT2DirectoryEntry);

        // Lewati entry kosong
        if (entry->inode == 0) break;

        // Lewati "." dan ".."
        if (!(entry->name_len == 1 && memcmp(name_ptr, ".", 1) == 0) &&
            !(entry->name_len == 2 && memcmp(name_ptr, "..", 2) == 0)) {
            return false; // Ada entry lain
        }

        offset += entry->rec_len;
    }

    // Hanya "." dan ".."
    return true;
}



/* =============================== CRUD FUNC ======================================== */

/**
 * @brief EXT2 Folder / Directory read
 * @param request buf point to struct EXT2 Directory
 * @return Error code: 0 success - 1 not a folder - 2 not found - 3 parent folder invalid - -1 unknown
 */
int8_t read_directory(struct EXT2DriverRequest *prequest);

/**
 * @brief EXT2 read, read a file from file system
 * @param request All attribute will be used except is_dir for read, buffer_size will limit reading count
 * @return Error code: 0 success - 1 not a file - 2 not enough buffer - 3 not found - 4 parent folder invalid - -1 unknown
 */
int8_t read(struct EXT2DriverRequest request);

/**
 * @brief EXT2 write, write a file or a folder to file system
 *
 * @param All attribute will be used for write except is_dir, buffer_size == 0 then create a folder / directory. It is possible that exist file with name same as a folder
 * @return Error code: 0 success - 1 file/folder already exist - 2 invalid parent folder - -1 unknown
 */
int8_t write(struct EXT2DriverRequest *request);

/**
 * @brief EXT2 delete, delete a file or empty directory in file system
 *  @param request buf and buffer_size is unused, is_dir == true means delete folder (possible file with name same as folder)
 * @return Error code: 0 success - 1 not found - 2 folder is not empty - 3 parent folder invalid -1 unknown
 */
int8_t delete(struct EXT2DriverRequest request);

/* =============================== MEMORY ==========================================*/

/**
 * @brief get a free inode from the disk, assuming it is always
 * available
 * @return new inode
 */
uint32_t allocate_node(void); 

/**
 * @brief deallocate node from the disk, will also deallocate its used blocks
 * also all of the blocks of indirect blocks if necessary
 * @param inode that needs to be deallocated
 */
void deallocate_node(uint32_t inode);

/**
 * @brief deallocate node blocks
 * @param locations node->block
 * @param blocks number of blocks
 */
void deallocate_blocks(void *loc, uint32_t blocks);

/**
 * @brief deallocate block from the disk
 * @param locations block locations
 * @param blocks number of blocks
 * @param bitmap block bitmap
 * @param depth depth of the block
 * @param last_bgd last bgd that is used
 * @param bgd_loaded whether bgd is loaded or not
 * @return new last bgd
 */
uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded);

/**
 * @brief write node->block in the given node, will allocate
 * at least node->blocks number of blocks, if first 12 item of node-> block
 * is not enough, will use indirect blocks
 * @param ptr the buffer that needs to be written
 * @param node pointer of the node
 * @param preffered_bgd it is located at the node inode bgd
 * 
 * @attention only implement until doubly indirect block, if you want to implement triply indirect block please increase the storage size to at least 256MB
 */
void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd);

/**
 * @brief update the node to the disk
 * @param node pointer of node
 * @param inode location of the node
 */
void sync_node(struct EXT2Inode *node, uint32_t inode);

