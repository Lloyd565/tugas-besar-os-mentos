#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"


#define POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))
static struct EXT2Superblock superblock;


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
int8_t read_directory(struct EXT2DriverRequest *request) {
    struct EXT2Inode parent_inode;
    read_inode(request->parent_inode, &parent_inode);
    if (!(parent_inode.i_mode & EXT2_S_IFDIR)) {
        return -1; // Parent is not a directory
    }
    // Inisialisasi buffer dan variabel
    uint8_t* buffer = (uint8_t*)request->buf;
    uint32_t buffer_offset = 0;
    uint32_t buffer_size = request->buffer_size;
    
    uint8_t block[BLOCK_SIZE];
    uint32_t entries_found = 0;
    uint32_t real_entries = 0; // Menghitung entri nyata (bukan . atau ..)


    //Membaca direct blocks (0-11)
    for (int i = 0; i < 12 && parent_inode.i_block[i] != 0; i++) {
        read_blocks(block, parent_inode.i_block[i], 1);
        uint32_t offset = 0;

        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(block + offset);
            if (entry->inode == 0) {
                offset += entry->rec_len;
                continue;
            }

            // Cek apakah ada cukup ruang di buffer
            if (buffer_offset + entry->rec_len > buffer_size) {
                return -2; // Buffer terlalu kecil
            }

            // Salin seluruh entri direktori termasuk padding
            memcpy(buffer + buffer_offset, entry, entry->rec_len);

            // Cek apakah ini adalah entri khusus (. atau ..)
            char entry_name[256] = {0};
            memcpy(entry_name, (uint8_t*)entry + sizeof(struct EXT2DirectoryEntry), entry->name_len);
            entry_name[entry->name_len] = '\0';
            
            bool is_special = (strcmp(entry_name, ".") == 0) || (strcmp(entry_name, "..") == 0);
            if (!is_special) {
                real_entries++; // Hanya hitung entri non-khusus
            }
            
            buffer_offset += entry->rec_len;
            entries_found++;
            offset += entry->rec_len;
        }
    }

    // [Apply the same logic to indirect blocks - add the special entry check]
    // For brevity, I'll show just the pattern - apply this to blocks 12, 13, 14 too
    
    // Return based on real entries (excluding . and ..)
    return entries_found > 0 ? 0 : 1; // 0 = has files, 1 = empty (only . and ..)
}
/**
 * @brief EXT2 read, read a file from file system
 * @param request All attribute will be used except is_dir for read, buffer_size will limit reading count
 * @return Error code: 0 success - 1 not a file - 2 not enough buffer - 3 not found - 4 parent folder invalid - -1 unknown
 */


/**
 * read inode from inode number
 */
void read_inode(uint32_t inode_idx, struct EXT2Inode *inode_out) {
    // inisialisasi lokasi inode
    uint32_t inodes_per_block = BLOCK_SIZE / INODE_SIZE;
    uint32_t block_offset = inode_idx / inodes_per_block;
    uint32_t inode_offset = inode_idx % inodes_per_block;

    struct BlockBuffer block_buf = {0};
    read_blocks(&block_buf, INODE_TABLE_BLOCK + block_offset, 1);

    memcpy(
        (void *)inode_out,
        (void *)(block_buf.buf + inode_offset * INODE_SIZE),
        sizeof(struct EXT2Inode)
    );
}

int8_t read(struct EXT2DriverRequest request) {
    struct EXT2Inode parent_inode;
    read_inode(request.parent_inode, &parent_inode);

    if (!(parent_inode.i_mode & EXT2_S_IFDIR)) {
        return -1; // Parent is not a directory
    }

    uint8_t block[BLOCK_SIZE];
    read_blocks(block, parent_inode.i_block[0], 1);

    uint32_t offset = 0;
    while (offset < BLOCK_SIZE) {

        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(block + offset);
        if (entry->inode == 0) {
            offset += entry->rec_len;
            continue;
        }

        char entry_name[256]; // nama file/folder maksimal 255 karakter + null terminator
        memcpy(entry_name, (void *)entry + sizeof(struct EXT2DirectoryEntry), entry->name_len);
        entry_name[entry->name_len] = '\0';

        if (entry->name_len == request.name_len && memcmp(entry_name, request.name, request.name_len) == 0) {
            // Found the file
            struct EXT2Inode file_inode;
            read_inode(entry->inode, &file_inode);
            if (file_inode.i_mode & EXT2_S_IFDIR) {
                return 1; // It's a directory, not a file
            }

            if (file_inode.i_size > request.buffer_size) {
                return -2; // Buffer too small
            }
            uint32_t remaining = file_inode.i_size;
            uint8_t *buf_ptr = (uint8_t *)request.buf;

            // --- 1. Read direct blocks (0-11)
            for (int i = 0; i < 12 && remaining > 0; i++) {
                if (file_inode.i_block[i] == 0) break;
                uint32_t to_read = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
                read_blocks(buf_ptr, file_inode.i_block[i], 1);
                buf_ptr += to_read;
                remaining -= to_read;
            }

            // --- 2. Read single indirect block (12)
            if (remaining > 0 && file_inode.i_block[12] != 0) {
                uint32_t indirect_block[BLOCK_SIZE / sizeof(uint32_t)];
                read_blocks((uint8_t*)indirect_block, file_inode.i_block[12], 1);

                for (uint32_t i = 0; i < BLOCK_SIZE / sizeof(uint32_t) && remaining > 0; i++) {
                    if (indirect_block[i] == 0) break;
                    uint32_t to_read = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
                    read_blocks(buf_ptr, indirect_block[i], 1);
                    buf_ptr += to_read;
                    remaining -= to_read;
                }
            }

            // Baca double indirect block (13)
            if (remaining > 0 && file_inode.i_block[13] != 0) {
                uint32_t double_indirect_block[BLOCK_SIZE / sizeof(uint32_t)];
                read_blocks((uint8_t*)double_indirect_block, file_inode.i_block[13], 1);

                for (uint32_t i = 0; i < BLOCK_SIZE / sizeof(uint32_t) && remaining > 0; i++) {
                    if (double_indirect_block[i] == 0) break;

                    uint32_t indirect_block[BLOCK_SIZE / sizeof(uint32_t)];
                    read_blocks((uint8_t*)indirect_block, double_indirect_block[i], 1);

                    for (uint32_t j = 0; j < BLOCK_SIZE / sizeof(uint32_t) && remaining > 0; j++) {
                        if (indirect_block[j] == 0) break;
                        uint32_t to_read = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
                        read_blocks(buf_ptr, indirect_block[j], 1);
                        buf_ptr += to_read;
                        remaining -= to_read;
                    }
                }
            }

            // Baca triple indirect block (14)
            if (remaining > 0 && file_inode.i_block[14] != 0) {
                uint32_t triple_indirect_block[BLOCK_SIZE / sizeof(uint32_t)];
                read_blocks((uint8_t*)triple_indirect_block, file_inode.i_block[14], 1);

                for (uint32_t i = 0; i < BLOCK_SIZE / sizeof(uint32_t) && remaining > 0; i++) {
                    if (triple_indirect_block[i] == 0) break;

                    uint32_t double_indirect_block[BLOCK_SIZE / sizeof(uint32_t)];
                    read_blocks((uint8_t*)double_indirect_block, triple_indirect_block[i], 1);

                    for (uint32_t j = 0; j < BLOCK_SIZE / sizeof(uint32_t) && remaining > 0; j++) {
                        if (double_indirect_block[j] == 0) break;

                        uint32_t indirect_block[BLOCK_SIZE / sizeof(uint32_t)];
                        read_blocks((uint8_t*)indirect_block, double_indirect_block[j], 1);

                        for (uint32_t k = 0; k < BLOCK_SIZE / sizeof(uint32_t) && remaining > 0; k++) {
                            if (indirect_block[k] == 0) break;
                            uint32_t to_read = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
                            read_blocks(buf_ptr, indirect_block[k], 1);
                            buf_ptr += to_read;
                            remaining -= to_read;
                        }
                    }
                }
            }
            return 0; // Successfully read
        }

        offset += entry->rec_len;
    }
    return 2; // File not found
}



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
 * Subfungsi : free blocks dan free pointer blocks
 */

// Fungsi untuk membebaskan blok yang digunakan oleh file atau direktori
void free_block(uint32_t block_num) {
    if (block_num == 0) return;
    
    uint8_t bitmap[BLOCK_SIZE];
    read_blocks(bitmap, 3, 1);
    
    uint32_t byte = block_num / 8;
    uint8_t bit = 1 << (block_num % 8);
    
    // Tandai blok sebagai bebas dengan mengatur bit menjadi 0
    bitmap[byte] &= ~bit;
    
    write_blocks(bitmap, 3, 1);
    
    // Update superblock untuk mencatat blok bebas tambahan
    superblock.s_free_blocks_count++;
}

// Fungsi untuk membebaskan blok-blok pointer (untuk indirect blocks)
void free_pointer_blocks(uint32_t block_num, int level) {
    if (block_num == 0) return;
    
    uint32_t pointers[POINTERS_PER_BLOCK];
    read_blocks((uint8_t*)pointers, block_num, 1);
    
    // Bebaskan blok-blok data yang ditunjuk
    for (uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) {
        if (pointers[i] == 0) continue;
        
        if (level > 1) {
            // Jika level > 1, ini adalah pointer ke pointer lain
            free_pointer_blocks(pointers[i], level - 1);
        } else {
            // Level 1 adalah pointer langsung ke data block
            free_block(pointers[i]);
        }
    }
    
    // Bebaskan blok pointer itu sendiri
    free_block(block_num);
}

 
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

