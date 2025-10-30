#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"


#define POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))
static struct EXT2Superblock superblock;
static struct EXT2BlockGroupDescriptor bgd;


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

// Fungsi untuk memeriksa apakah direktori kosong atau tidak
bool is_directory_empty(uint32_t inode_num) {
    struct EXT2Inode inode;
    read_inode(inode_num, &inode);
    
    if (!(inode.i_mode & EXT2_S_IFDIR)) {
        return false; // Bukan direktori
    }
    
    uint8_t block[BLOCK_SIZE];
    read_blocks(block, inode.i_block[0], 1);
    
    // Periksa entri dalam direktori
    uint32_t offset = 0;
    int entries = 0;
    
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(block + offset);
        if (entry->inode != 0) {
            entries++;
            
            // Abaikan entri "." dan ".."
            char entry_name[256];
            memcpy(entry_name, (uint8_t*)entry + sizeof(struct EXT2DirectoryEntry), entry->name_len);
            entry_name[entry->name_len] = '\0';
            
            // Ganti strcmp dengan pengecekan manual
            bool is_dot = (entry->name_len == 1 && entry_name[0] == '.');
            bool is_dotdot = (entry->name_len == 2 && entry_name[0] == '.' && entry_name[1] == '.');
            
            if (!(entries <= 2 && (is_dot || is_dotdot))) {
                return false; // Direktori tidak kosong
            }
        }
        
        if (entry->rec_len == 0) break;
        offset += entry->rec_len;
    }
    
    // Direktori kosong jika hanya berisi entri "." dan ".."
    return entries <= 2;
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

    
    // Return berdasarkan ada tidaknya entri nyata
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
        return -1; // Parentnya file
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
            // File nya ditemukan di direktori parent
            struct EXT2Inode file_inode;
            read_inode(entry->inode, &file_inode);
            if (file_inode.i_mode & EXT2_S_IFDIR) {
                return 1; // Bukan file
            }

            if (file_inode.i_size > request.buffer_size) {
                return -2; // Buffer kecil
            }
            uint32_t remaining = file_inode.i_size;
            uint8_t *buf_ptr = (uint8_t *)request.buf;

            //Baca direct blocks (0-11)
            for (int i = 0; i < 12 && remaining > 0; i++) {
                if (file_inode.i_block[i] == 0) break;
                uint32_t to_read = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
                read_blocks(buf_ptr, file_inode.i_block[i], 1);
                buf_ptr += to_read;
                remaining -= to_read;
            }

            //Baca indirect block tunggal(12)
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
int8_t write(struct EXT2DriverRequest *request) {
    //Baca inode parent
    struct EXT2Inode parent_inode;
    read_inode(request->parent_inode, &parent_inode);
    if (!(parent_inode.i_mode & EXT2_S_IFDIR)) {
        return 2; // Parent tidak valid (bukan directory)
    }

    // Cek apakah sudah ada entry dengan nama yang sama
    uint8_t parent_dir_block[BLOCK_SIZE];
    read_blocks(parent_dir_block, parent_inode.i_block[0], 1);
    uint32_t offset = 0;
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(parent_dir_block + offset);
        if (entry->inode != 0) {
            if (entry->name_len == request->name_len &&
                memcmp((uint8_t *)entry + sizeof(struct EXT2DirectoryEntry), request->name, request->name_len) == 0) {
                return 1; // File or directory already exists
            }
        }
        offset += entry->rec_len;
    }

    //Alokasikan inode baru
    uint32_t new_inode_no = allocate_node();
    if (new_inode_no == 0) return -1;

    struct EXT2Inode new_inode;
    memset(&new_inode, 0, sizeof(new_inode));

    if (request->buffer_size > 0) {
        new_inode.i_mode = EXT2_S_IFREG;
        new_inode.i_size = request->buffer_size;
        // Hitung jumlah blok yang dibutuhkan (ceil)
        uint32_t num_blocks = (request->buffer_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        uint32_t remaining_size = request->buffer_size;
        uint8_t *buf_ptr = (uint8_t *)request->buf;
        uint32_t written_blocks = 0;

        // Direct Blocks (indeks 0 s/d 11)
        uint32_t n_direct = (num_blocks > 12) ? 12 : num_blocks;
        for (uint32_t i = 0; i < n_direct; i++) {
            uint32_t blk = allocate_block();
            if (blk == 0) return -1;
            new_inode.i_block[i] = blk;
            uint32_t to_write = (remaining_size < BLOCK_SIZE) ? remaining_size : BLOCK_SIZE;
            write_blocks(buf_ptr, blk, 1);
            buf_ptr += to_write;
            remaining_size = (remaining_size > BLOCK_SIZE) ? remaining_size - BLOCK_SIZE : 0;
            written_blocks++;
        }

        //Single Indirect Block (indeks 12)
        if (num_blocks > 12) {
            uint32_t single_needed = (num_blocks - 12 > POINTERS_PER_BLOCK) ? POINTERS_PER_BLOCK : (num_blocks - 12);
            uint32_t single_indirect = allocate_block();
            if (single_indirect == 0) return -1;
            new_inode.i_block[12] = single_indirect;
            uint32_t pointers[POINTERS_PER_BLOCK] = {0};
            for (uint32_t i = 0; i < single_needed; i++) {
                uint32_t blk = allocate_block();
                if (blk == 0) return -1;
                pointers[i] = blk;
                uint32_t to_write = (remaining_size < BLOCK_SIZE) ? remaining_size : BLOCK_SIZE;
                write_blocks(buf_ptr, blk, 1);
                buf_ptr += to_write;
                remaining_size = (remaining_size > BLOCK_SIZE) ? remaining_size - BLOCK_SIZE : 0;
                written_blocks++;
            }
            write_pointer_block(single_indirect, pointers);
        }

        //Double Indirect Block (indeks 13)
        if (num_blocks > (12 + POINTERS_PER_BLOCK)) {
            uint32_t blocks_left = num_blocks - (12 + POINTERS_PER_BLOCK);
            uint32_t doubly_indirect = allocate_block();
            if (doubly_indirect == 0) return -1;
            new_inode.i_block[13] = doubly_indirect;
            uint32_t pointerBlock[POINTERS_PER_BLOCK] = {0};
            for (uint32_t i = 0; i < POINTERS_PER_BLOCK && blocks_left > 0; i++) {
                uint32_t indirectBlock = allocate_block();
                if (indirectBlock == 0) return -1;
                pointerBlock[i] = indirectBlock;
                uint32_t indirectPointers[POINTERS_PER_BLOCK] = {0};
                for (uint32_t j = 0; j < POINTERS_PER_BLOCK && blocks_left > 0; j++) {
                    uint32_t dataBlock = allocate_block();
                    if (dataBlock == 0) return -1;
                    indirectPointers[j] = dataBlock;
                    uint32_t to_write = (remaining_size < BLOCK_SIZE) ? remaining_size : BLOCK_SIZE;
                    write_blocks(buf_ptr, dataBlock, 1);
                    buf_ptr += to_write;
                    remaining_size = (remaining_size > BLOCK_SIZE) ? remaining_size - BLOCK_SIZE : 0;
                    blocks_left--;
                    written_blocks++;
                }
                write_pointer_block(indirectBlock, indirectPointers);
            }
            write_pointer_block(doubly_indirect, pointerBlock);
        }

        //Triply Indirect Block (indeks 14)
        if (num_blocks > (12 + POINTERS_PER_BLOCK + POINTERS_PER_BLOCK * POINTERS_PER_BLOCK)) {
            uint32_t blocks_left = num_blocks - (12 + POINTERS_PER_BLOCK + POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);
            uint32_t triply_indirect = allocate_block();
            if (triply_indirect == 0) return -1;
            new_inode.i_block[14] = triply_indirect;
            uint32_t pointers_level1[POINTERS_PER_BLOCK] = {0};
            for (uint32_t i = 0; i < POINTERS_PER_BLOCK && blocks_left > 0; i++) {
                uint32_t doubly_ind = allocate_block();
                if (doubly_ind == 0) return -1;
                pointers_level1[i] = doubly_ind;
                uint32_t pointers_level2[POINTERS_PER_BLOCK] = {0};
                for (uint32_t j = 0; j < POINTERS_PER_BLOCK && blocks_left > 0; j++) {
                    uint32_t single_ind = allocate_block();
                    if (single_ind == 0) return -1;
                    pointers_level2[j] = single_ind;
                    uint32_t pointers_level3[POINTERS_PER_BLOCK] = {0};
                    for (uint32_t k = 0; k < POINTERS_PER_BLOCK && blocks_left > 0; k++) {
                        uint32_t dataBlock = allocate_block();
                        if (dataBlock == 0) return -1;
                        pointers_level3[k] = dataBlock;
                        uint32_t to_write = (remaining_size < BLOCK_SIZE) ? remaining_size : BLOCK_SIZE;
                        write_blocks(buf_ptr, dataBlock, 1);
                        buf_ptr += to_write;
                        remaining_size = (remaining_size > BLOCK_SIZE) ? remaining_size - BLOCK_SIZE : 0;
                        blocks_left--;
                        written_blocks++;
                    }
                    write_pointer_block(single_ind, pointers_level3);
                }
                write_pointer_block(doubly_ind, pointers_level2);
            }
            write_pointer_block(triply_indirect, pointers_level1);
        }
    } else {
        // Penanganan untuk direktori
        new_inode.i_mode = EXT2_S_IFDIR;
        new_inode.i_size = BLOCK_SIZE;
        new_inode.i_blocks = 1;
        uint32_t block_alloc = allocate_block();
        if (block_alloc == 0) return -1;
        new_inode.i_block[0] = block_alloc;
        uint8_t dir_block[BLOCK_SIZE] = {0};
        uint32_t off = 0;
        struct EXT2DirectoryEntry *dot = (struct EXT2DirectoryEntry *)(dir_block + off);
        dot->inode = new_inode_no;
        dot->rec_len = 12;
        dot->name_len = 1;
        dot->file_type = EXT2_FT_DIR;
        memcpy((uint8_t *)dot + sizeof(struct EXT2DirectoryEntry), ".", 1);
        off += dot->rec_len;
        struct EXT2DirectoryEntry *dotdot = (struct EXT2DirectoryEntry *)(dir_block + off);
        dotdot->inode = request->parent_inode;
        dotdot->rec_len = BLOCK_SIZE - off;
        dotdot->name_len = 2;
        dotdot->file_type = EXT2_FT_DIR;
        memcpy((uint8_t *)dotdot + sizeof(struct EXT2DirectoryEntry), "..", 2);
        write_blocks(dir_block, new_inode.i_block[0], 1);
    }

    // Simpan inode baru ke disk
    write_inode(new_inode_no, &new_inode);

    // Tambahkan entri direktori pada parent - ALWAYS APPEND AT END
    read_blocks(parent_dir_block, parent_inode.i_block[0], 1);
    
    // Cari entri valid terakhir
    uint32_t curr_off = 0;
    struct EXT2DirectoryEntry *last_valid_entry = NULL;
    
    while (curr_off < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(parent_dir_block + curr_off);
        
        if (entry->rec_len == 0) break; // Invalid entry
        
        if (entry->inode != 0) {
            last_valid_entry = entry;
        }
        
        curr_off += entry->rec_len;
        
        //Break jika sudah melewati batas blok
        if (curr_off >= BLOCK_SIZE) break;
    }
    
    if (!last_valid_entry) {
        return -1; // No valid entries found
    }
    
    // Hitung ukuran entry baru
    uint16_t new_entry_size = 8 + (((request->name_len) + 3) & ~3);

    // Hitung ukuran ideal untuk entry terakhir
    uint16_t last_ideal_size = 8 + (((last_valid_entry->name_len) + 3) & ~3);
    if (last_valid_entry->name_len % 4 == 0) {
        last_ideal_size += 4;
    }
    
    // Hitung ruang yang tersedia setelah mengecilkan entry terakhir
    uint32_t available_space = last_valid_entry->rec_len - last_ideal_size;
    
    if (available_space < new_entry_size) {
        return -1; // Ruang direktori penuh
    }
    
    // Kecilkan entry terakhir ke ukuran idealnya
    last_valid_entry->rec_len = last_ideal_size;
    
    // Buat entry baru di ruang yang tersedia
    struct EXT2DirectoryEntry *new_entry = 
        (struct EXT2DirectoryEntry *)((uint8_t*)last_valid_entry + last_ideal_size);
    
    new_entry->inode = new_inode_no;
    new_entry->rec_len = available_space;
    new_entry->name_len = request->name_len;
    new_entry->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    
    memcpy((uint8_t *)new_entry + sizeof(struct EXT2DirectoryEntry), 
           request->name, request->name_len);
    
    // Tulis kembali blok direktori parent ke disk
    write_blocks(parent_dir_block, parent_inode.i_block[0], 1);
    
    // Perbarui inode parent (jumlah link bertambah jika direktori)
    write_inode(request->parent_inode, &parent_inode);
    
    return 0; // Success
}

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

 
int8_t delete(struct EXT2DriverRequest request) {
    // Validasi parent inode (harus direktori)
    struct EXT2Inode parent_inode;
    read_inode(request.parent_inode, &parent_inode);
    
    if (!(parent_inode.i_mode & EXT2_S_IFDIR)) {
        return -1; // Parent bukan direktori
    }

    // Cari file/direktori yang akan dihapus di dalam parent
    uint8_t parent_block[BLOCK_SIZE];
    read_blocks(parent_block, parent_inode.i_block[0], 1);
    
    uint32_t current_offset = 0;
    // uint32_t prev_offset = 0;
    struct EXT2DirectoryEntry *current_entry = NULL;
    struct EXT2DirectoryEntry *prev_entry = NULL;
    bool found = false;
    
    while (current_offset < BLOCK_SIZE) {
        current_entry = (struct EXT2DirectoryEntry *)(parent_block + current_offset);
        
        // Skip entri kosong atau invalid
        if (current_entry->inode == 0 || current_entry->rec_len == 0) {
            // prev_offset = current_offset;
            current_offset += current_entry->rec_len > 0 ? current_entry->rec_len : 8; // Pastikan minimal 8 byte
            prev_entry = current_entry;
            continue;
        }
        
        // Ekstrak dan bandingkan nama
        if (current_entry->name_len == request.name_len) {
            char entry_name[256];
            memcpy(entry_name, (uint8_t*)current_entry + sizeof(struct EXT2DirectoryEntry), current_entry->name_len);
            entry_name[current_entry->name_len] = '\0';
            
            if (memcmp(entry_name, request.name, request.name_len) == 0) {
                found = true;
                break;
            }
        }
        
        // prev_offset = current_offset;
        current_offset += current_entry->rec_len;
        prev_entry = current_entry;
    }
    
    if (!found) {
        return 1; // Entry tidak ditemukan
    }
    
    // Baca inode dari file/direktori yang akan dihapus
    uint32_t target_inode_num = current_entry->inode;
    struct EXT2Inode target_inode;
    read_inode(target_inode_num, &target_inode);
    
    //Jika ini adalah direktori, pastikan kosong
    if (target_inode.i_mode & EXT2_S_IFDIR) {
        if (!is_directory_empty(target_inode_num)) {
            return 2; // Folder tidak kosong
        }
    }

    // Bebaskan blok yang digunakan oleh file/direktori

    //Bebaskan direct blocks
    for (int i = 0; i < 12; i++) {
        if (target_inode.i_block[i] != 0) {
            free_block(target_inode.i_block[i]);
        }
    }
    
    // Bebaskan single indirect block
    if (target_inode.i_block[12] != 0) {
        free_pointer_blocks(target_inode.i_block[12], 1);
    }

    // Bebaskan double indirect block
    if (target_inode.i_block[13] != 0) {
        free_pointer_blocks(target_inode.i_block[13], 2);
    }

    // Bebaskan triple indirect block
    if (target_inode.i_block[14] != 0) {
        free_pointer_blocks(target_inode.i_block[14], 3);
    }
    
    // Tandai inode sebagai tidak terpakai
    uint8_t inode_bitmap[BLOCK_SIZE];
    read_blocks(inode_bitmap, 4, 1); // Asumsi bitmap inode di blok 4
    
    uint32_t inode_byte = target_inode_num / 8;
    uint8_t inode_bit = 1 << (target_inode_num % 8);
    inode_bitmap[inode_byte] &= ~inode_bit;
    
    write_blocks(inode_bitmap, 4, 1);
    
    // Update directory entry untuk menghapus referensi
    

    if (current_offset == 0) {
        current_entry->inode = 0; 
    } 
    else if (current_offset + current_entry->rec_len >= BLOCK_SIZE) {
        if (prev_entry) {
            prev_entry->rec_len += current_entry->rec_len;
        }
    } 
    else {
        current_entry->inode = 0;   
    }

    //Tulis blok direktori yang telah dimodifikasi
    write_blocks(parent_block, parent_inode.i_block[0], 1);

    // Update superblock dan bgd
    superblock.s_free_inodes_count++;
    bgd.bg_free_inodes_count++;
    
    if (target_inode.i_mode & EXT2_S_IFDIR) {
        bgd.bg_used_dirs_count--;
    }

    // Tulis kembali superblock dan bgd
    write_blocks(&superblock, 1, 1);
    write_blocks(&bgd, 2, 1);
    
    return 0; // Operasi berhasil
}

/* =============================== MEMORY ==========================================*/

/**
 * @brief get a free inode from the disk, assuming it is always
 * available
 * @return new inode
 */
uint32_t allocate_node(void) {
    uint8_t bitmap[BLOCK_SIZE];
    read_blocks(bitmap, 4, 1); // Inode bitmap block
    
    // Find first free inode
    for (uint32_t i = 0; i < superblock.s_inodes_count; i++) {
        uint32_t byte = i / 8;
        uint8_t bit = 1 << (i % 8);
        if (!(bitmap[byte] & bit)) {
            // Mark as used
            bitmap[byte] |= bit;
            write_blocks(bitmap, 4, 1);
            // Update superblock
            superblock.s_free_inodes_count--;
            write_blocks(&superblock, 1, 1);
            // return inode number (1-based)
            return i + 1;
        }
    }
    return 0; // No free inodes
}

/**
 * @brief deallocate node from the disk, will also deallocate its used blocks
 * also all of the blocks of indirect blocks if necessary
 * @param inode that needs to be deallocated
 */
void deallocate_node(uint32_t inode_num) {
    if (inode_num == 0) return;

    struct EXT2Inode inode;
    read_inode(inode_num, &inode);

    //bebaskan direct blocks 
    for (int i = 0; i < 12; i++) {
        if (inode.i_block[i] != 0) free_block(inode.i_block[i]);
    }

    //single, double, triple indirect 
    if (inode.i_block[12] != 0) free_pointer_blocks(inode.i_block[12], 1);
    if (inode.i_block[13] != 0) free_pointer_blocks(inode.i_block[13], 2);
    if (inode.i_block[14] != 0) free_pointer_blocks(inode.i_block[14], 3);

    // clear inode on disk 
    struct EXT2Inode zero = {0};
    write_inode(inode_num, &zero);

    // update inode bitmap (as in delete)
    uint8_t inode_bitmap[BLOCK_SIZE];
    read_blocks(inode_bitmap, INODE_BITMAP_BLOCK, 1);
    uint32_t byte = (inode_num - 1) / 8; /* if bitmap indexed from inode 1 */
    uint8_t bit = 1 << ((inode_num - 1) % 8);
    inode_bitmap[byte] &= ~bit;
    write_blocks(inode_bitmap, INODE_BITMAP_BLOCK, 1);

    // update counters
    superblock.s_free_inodes_count++;
    bgd.bg_free_inodes_count++;
    write_blocks(&superblock, SUPERBLOCK_BLOCK, 1);
    write_blocks(&bgd, BGDT_BLOCK, 1);
}

/**
 * @brief deallocate node blocks
 * @param locations node->block
 * @param blocks number of blocks
 */
void deallocate_blocks(void *loc, uint32_t blocks) {
    uint32_t *locations = (uint32_t *)loc;
    for (uint32_t i = 0; i < blocks; i++) {
        if (locations[i] != 0) free_block(locations[i]);
    }
}

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
uint32_t deallocate_block(uint32_t *locations, uint32_t blocks, struct BlockBuffer *bitmap, uint32_t depth, uint32_t *last_bgd, bool bgd_loaded) {
    if (!locations || blocks == 0) return last_bgd ? *last_bgd : 0;

    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t loc = locations[i];
        if (loc == 0) continue;

        if (depth > 1) {
            /* loc is a pointer block: read pointers and recurse */
            uint32_t pointers[POINTERS_PER_BLOCK];
            read_blocks((uint8_t*)pointers, loc, 1);
            deallocate_block(pointers, POINTERS_PER_BLOCK, bitmap, depth - 1, last_bgd, bgd_loaded);
        }

        /* Clear bitmap bit for this block */
        if (bitmap) {
            uint32_t byte = loc / 8;
            uint8_t bit = 1 << (loc % 8);
            bitmap->buf[byte] &= ~bit;
            write_blocks(bitmap, BLOCK_BITMAP_BLOCK, 1);
        } else {
            uint8_t bm[BLOCK_SIZE];
            read_blocks(bm, BLOCK_BITMAP_BLOCK, 1);
            uint32_t byte = loc / 8;
            uint8_t bit = 1 << (loc % 8);
            bm[byte] &= ~bit;
            write_blocks(bm, BLOCK_BITMAP_BLOCK, 1);
        }

        /* Update superblock counters */
        superblock.s_free_blocks_count++;
    }

    /* return last_bgd unchanged for now */
    if (last_bgd) return *last_bgd;
    return 0;
}
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
void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd) {
    if (!node) return;

    uint32_t num_blocks = (node->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint32_t remaining = node->i_size;
    uint8_t *data = (uint8_t *)ptr;
    uint32_t written = 0;

    /* Direct blocks (0..11) */
    uint32_t n_direct = (num_blocks > 12) ? 12 : num_blocks;
    for (uint32_t i = 0; i < n_direct; i++) {
        uint32_t blk = allocate_block();
        if (blk == 0) return; // tidak cukup ruang
        node->i_block[i] = blk;
        uint32_t to_write = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        write_blocks(data, blk, 1);
        data += to_write;
        remaining = (remaining > BLOCK_SIZE) ? remaining - BLOCK_SIZE : 0;
        written++;
    }

    /* Single indirect (index 12) */
    if (written < num_blocks) {
        uint32_t need = num_blocks - written;
        uint32_t to_fill = (need > POINTERS_PER_BLOCK) ? POINTERS_PER_BLOCK : need;
        uint32_t single = allocate_block();
        if (single == 0) return;
        node->i_block[12] = single;
        uint32_t pointers[POINTERS_PER_BLOCK];
        for (uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) pointers[i] = 0;

        for (uint32_t i = 0; i < to_fill; i++) {
            uint32_t blk = allocate_block();
            if (blk == 0) return;
            pointers[i] = blk;
            uint32_t to_write = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
            write_blocks(data, blk, 1);
            data += to_write;
            remaining = (remaining > BLOCK_SIZE) ? remaining - BLOCK_SIZE : 0;
            written++;
        }
        write_pointer_block(single, pointers);
    }

    /* Double indirect (index 13) */
    if (written < num_blocks) {
        uint32_t blocks_left = num_blocks - written;
        uint32_t doubly = allocate_block();
        if (doubly == 0) return;
        node->i_block[13] = doubly;
        uint32_t first_level[POINTERS_PER_BLOCK];
        for (uint32_t i = 0; i < POINTERS_PER_BLOCK; i++) first_level[i] = 0;

        for (uint32_t i = 0; i < POINTERS_PER_BLOCK && blocks_left > 0; i++) {
            uint32_t indirect = allocate_block();
            if (indirect == 0) return;
            first_level[i] = indirect;
            uint32_t second_level[POINTERS_PER_BLOCK];
            for (uint32_t j = 0; j < POINTERS_PER_BLOCK; j++) second_level[j] = 0;

            for (uint32_t j = 0; j < POINTERS_PER_BLOCK && blocks_left > 0; j++) {
                uint32_t data_blk = allocate_block();
                if (data_blk == 0) return;
                second_level[j] = data_blk;
                uint32_t to_write = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
                write_blocks(data, data_blk, 1);
                data += to_write;
                remaining = (remaining > BLOCK_SIZE) ? remaining - BLOCK_SIZE : 0;
                blocks_left--;
                written++;
            }
            write_pointer_block(indirect, second_level);
        }
        write_pointer_block(doubly, first_level);
    }

    /* Triply indirect not implemented here (comment in header) */

    /* Update i_blocks (count of 512-byte sectors? here count blocks as BLOCK_SIZE units) */
    node->i_blocks = written; // simple block count; adjust if you count 512-byte sectors
}
/**
 * @brief update the node to the disk
 * @param node pointer of node
 * @param inode location of the node
 */
void sync_node(struct EXT2Inode *node, uint32_t inode) {
    if (!node || inode == 0) return;

    uint32_t inodes_per_block = BLOCK_SIZE / INODE_SIZE;
    uint32_t block_offset = (inode - 1) / inodes_per_block;
    uint32_t inode_offset = (inode - 1) % inodes_per_block;

    struct BlockBuffer blk = {0};
    read_blocks(&blk, INODE_TABLE_BLOCK + block_offset, 1);

    memcpy(blk.buf + inode_offset * INODE_SIZE, node, sizeof(struct EXT2Inode));
    write_blocks(&blk, INODE_TABLE_BLOCK + block_offset, 1);
}
