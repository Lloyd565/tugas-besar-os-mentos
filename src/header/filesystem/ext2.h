#ifndef _EXT2_H
#define _EXT2_H

#include "../driver/disk.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../stdlib/stdtype.h"


/* --  File System constants -- */
#define BOOT_SECTOR 0                         // legacy from FAT32 filesystem IF2130 OS
#define DISK_SPACE 4194304u                   // 4MB disk space (because our disk or storage.bin is 4MB)
#define EXT2_SUPER_MAGIC 0xEF53               // this indicating that the filesystem used by OS is ext2
#define INODE_SIZE sizeof(struct EXT2Inode)   // size of inode
#define INODES_PER_TABLE (BLOCK_SIZE / INODE_SIZE) // number of inode per block
#define GROUPS_COUNT (BLOCK_SIZE / sizeof(struct EXT2BlockGroupDescriptor)) / 2u // number of groups in the filesystem
#define BLOCKS_PER_GROUP (DISK_SPACE / BLOCK_SIZE / GROUPS_COUNT)                 // number of blocks per group
#define INODES_TABLE_BLOCK_COUNT 16u
#define INODES_PER_GROUP (INODES_PER_TABLE * INODES_TABLE_BLOCK_COUNT)            // number of inodes per group

/* -- To Help with double Inderect Block -- */
#define EXT2_DIRECT_BLOCK_COUNT 12
#define EXT2_INDIRECT_BLOCK EXT2_DIRECT_BLOCK_COUNT // 12
#define EXT2_DOUBLY_INDIRECT_BLOCK (EXT2_DIRECT_BLOCK_COUNT + 1) // 13
#define EXT2_BLOCK_PTR_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t)) // 128
#define EXT2_INDIRECT_CAPACITY EXT2_BLOCK_PTR_PER_BLOCK // 128
#define EXT2_DOUBLY_INDIRECT_CAPACITY (EXT2_BLOCK_PTR_PER_BLOCK * EXT2_BLOCK_PTR_PER_BLOCK) // 16384

/**
 * inodes constant 
 * - reference: https://www.nongnu.org/ext2-doc/ext2.html#inode-table
 */
#define EXT2_S_IFREG 0x8000 // regular file 
#define EXT2_S_IFDIR 0x4000 // directory


/* FILE TYPE CONSTANT*/
/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 * - Table 4.2. Defined Inode File Type Values
 */

#define EXT2_FT_UNKNOWN   0 // Unknown File Type
#define EXT2_FT_REG_FILE  1 // Regular File
#define EXT2_FT_DIR       2 // Directory
#define EXT2_FT_NEXT      3 // Character Special File


/**
 * EXT2DriverRequest
 * Derived and modified from FAT32DriverRequest legacy IF2130 OS 
 */
struct EXT2DriverRequest
{
    void     *buf; 
    char     *name; 
    uint8_t   name_len; 
    uint32_t  parent_inode; 
    uint32_t  buffer_size; 
    bool      is_directory; 
} __attribute__((packed));


/**
 * EXT2Superblock: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#superblock
 */
struct EXT2Superblock
{
    uint32_t s_inodes_count;        // total number of inodes
    uint32_t s_blocks_count;        // total number of blocks

    uint32_t s_r_blocks_count;      // reserved blocks for super user (unused here)
    uint32_t s_free_blocks_count;   // total number of free blocks
    uint32_t s_free_inodes_count;   // total number of free inodes
    uint32_t s_first_data_block;    // first data block (contains superblock)
    uint32_t s_first_ino;           // first usable inode

    uint32_t s_blocks_per_group;    // blocks per group
    uint32_t s_frags_per_group;     // frags per group
    uint32_t s_inodes_per_group;    // inodes per group

    uint16_t s_magic;               // EXT2_SUPER_MAGIC

    uint8_t  s_prealloc_blocks;     // prealloc blocks for files
    uint8_t  s_prealloc_dir_blocks; // prealloc blocks for directories
} __attribute__((packed));


/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table
 */
struct EXT2BlockGroupDescriptor
{
    uint32_t bg_block_bitmap;       // first block of block bitmap
    uint32_t bg_inode_bitmap;       // first block of inode bitmap
    uint32_t bg_inode_table;        // first block of inode table

    uint16_t bg_free_blocks_count;  // free blocks in group
    uint16_t bg_free_inodes_count;  // free inodes in group
    uint16_t bg_used_dirs_count;    // directories in group
    uint16_t bg_pad;                // padding

    uint32_t bg_reserved[3];        // reserved
} __attribute__((packed));


/**
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#block-group-descriptor-table
 */
struct EXT2BlockGroupDescriptorTable
{
    struct EXT2BlockGroupDescriptor table[GROUPS_COUNT];
};


/**
 * EXT2Inode
 * Simplified inode structure (only fields used by our driver).
 */
struct EXT2Inode
{
    uint16_t i_mode;        // file type & access rights
    uint32_t i_size;        // size in bytes
    uint32_t i_blocks;      // blocks count in 512-byte units
    uint32_t i_block[15];   // data block pointers (12 direct + 3 indirect)
} __attribute__((packed));


struct EXT2InodeTable
{
    struct EXT2Inode table[INODES_PER_GROUP];
};


/**
 * EXT2DirectoryEntry
 * Linked List Directory
 * reference: 
 * - https://www.nongnu.org/ext2-doc/ext2.html#linked-directories
 */
struct EXT2DirectoryEntry
{
    uint32_t inode;      // inode number, 0 if unused
    uint16_t rec_len;    // record length (to next entry)
    uint8_t  name_len;   // length of file name (8-bit, spec-compliant)
    uint8_t  file_type;  // EXT2_FT_*
} __attribute__((packed));


/* =================== HELPER FUNCTION PROTOTYPES =================== */

/**
 * get the name of the entry
 * @param entry the directory entry
 * @return the name of the entry
 */
char *get_entry_name(void *entry);

/**
 * get the directory entry from the buffer
 * @param ptr the buffer that contains the directory table
 * @param offset the offset of the entry 
 * @return the directory entry
 */
struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset);

/**
 * get the next directory entry from the current entry
 * @param entry the current entry
 * @return the next directory entry
 */
struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry);

/**
 * get the record length of the entry
 * @param name_len the length of the name of the entry
 * @return the record length of the entry
 */
uint16_t get_entry_record_len(uint8_t name_len);

/**
 * get the offset of the first child of the directory
 * @param ptr the buffer that contains the directory table
 * @return the offset of the first child of the directory
 */
uint32_t get_dir_first_child_offset(void *ptr);


/* =================== INODE / BGD HELPERS =================== */

/**
 * @brief get bgd index from inode, inode will starts at index 1
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return bgd index (0 to GROUP_COUNT - 1)
 */
uint32_t inode_to_bgd(uint32_t inode);

/**
 * @brief get inode local index in the corrresponding bgd
 * @param inode 1 to INODES_PER_GROUP * GROUP_COUNT
 * @return local index
 */
uint32_t inode_to_local(uint32_t inode);

/**
 * @brief write back superblock & BGDT to disk
 */
void commit_metadata(void);

/**
 * @brief read inode from disk
 */
void read_inode(uint32_t inode, struct EXT2Inode *out);

/**
 * @brief write inode to disk
 */
void write_inode(uint32_t inode_idx, const struct EXT2Inode *inode);


/* =================== FILESYSTEM INITIALIZATION =================== */

/**
 * @brief create a new directory using given node
 * first item of directory table is its node location (name will be .)
 * second item of directory is its parent location (name will be ..)
 * @param node pointer of inode
 * @param inode inode that already allocated
 * @param parent_inode inode of parent directory (if root directory, the parent is itself)
 */
void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode);

/**
 * @brief check whether filesystem signature is missing or not in boot sector
 *
 * @return true if memcmp(boot_sector, fs_signature) returning inequality
 */
bool is_empty_storage(void);

/**
 * @brief create a new EXT2 filesystem. Will write fs_signature into boot sector,
 * initialize super block, bgd table, block and inode bitmap, and create root directory
 */
void create_ext2(void);

/**
 * @brief Initialize file system driver state, if is_empty_storage() then create_ext2()
 * Else, read and cache super block (located at block 1) and bgd table (located at block 2) into state
 */
void initialize_filesystem_ext2(void);

/**
 * @brief check whether a directory table has children or not
 * @param inode of a directory table
 * @return true if first_child_entry->inode = 0
 */
bool is_directory_empty(uint32_t inode);


/* =================== BITMAP / ALLOCATION HELPERS =================== */

bool is_block_used(uint32_t block_number);
void set_block_used(uint32_t block_number, bool used);

bool is_inode_used(uint32_t inode);
void set_inode_used(uint32_t inode, bool used);

/**
 * @brief get a free data block from the disk
 * @return block number, or 0 if full
 */
uint32_t allocate_block(void);

/**
 * @brief get a free inode from the disk
 * @return new inode index (1-based), or 0 if full
 */
uint32_t allocate_node(void);

/**
 * Find directory entry with given name in a directory inode.
 */
struct EXT2DirectoryEntry *find_entry_in_dir(uint32_t dir_inode, char *name, uint8_t name_len);


/* =============================== CRUD API ======================================== */

/**
 * @brief EXT2 Folder / Directory read
 * @param request buf points to struct EXT2DirectoryEntry array (size 1 block)
 * @return Error code: 0 success - 1 not a folder - 2 not found - 3 parent folder invalid - -1 unknown
 */
int8_t read_directory(struct EXT2DriverRequest *request);

/**
 * @brief EXT2 read, read a file from file system
 * @param request All attribute will be used except is_directory for read, buffer_size will limit reading count
 * @return Error code: 0 success - 1 not a file - 2 not enough buffer - 3 not found - 4 parent folder invalid - -1 unknown
 */
int8_t read(struct EXT2DriverRequest request);

/**
 * @brief EXT2 write, write a file or a folder to file system
 *
 * @param request All attribute will be used for write. is_directory==true => create folder,
 *        else create regular file.
 * @return Error code: 0 success - 1 file/folder already exist - 2 invalid parent folder - -1 unknown
 */
int8_t write(struct EXT2DriverRequest *request);

/**
 * @brief EXT2 delete, delete a file or empty directory in file system
 * @param request buf and buffer_size are unused, is_directory indicates deleting dir or file
 * @return Error code: 0 success - 1 not found - 2 folder is not empty - 3 parent folder invalid -1 unknown
 */
int8_t delete(struct EXT2DriverRequest request);

// Helper function
void build_absolute_path(char *current_path, char *relative_path, char *result_path);

// Syscall 8 handler
int8_t get_inode(struct EXT2DriverRequest request, uint32_t *result_inode);

// Syscall 9 handler  
int8_t get_resolved_path(struct EXT2DriverRequest request, char *result_path);


#endif
