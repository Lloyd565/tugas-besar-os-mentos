#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "header/filesystem/ext2.h"
#include "header/driver/disk.h"
#include "header/stdlib/string.h"

// Global variable
uint8_t *image_storage;
uint8_t *file_buffer;

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            (uint8_t*) ptr + BLOCK_SIZE*i, 
            image_storage + BLOCK_SIZE*(logical_block_address+i), 
            BLOCK_SIZE
        );
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            image_storage + BLOCK_SIZE*(logical_block_address+i), 
            (uint8_t*) ptr + BLOCK_SIZE*i, 
            BLOCK_SIZE
        );
    }
}
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "inserter: ./inserter <file to insert> <parent cluster index> <storage>\n");
        return 1;
    }

    // Allocate big buffers on heap
    image_storage = malloc(4 * 1024 * 1024);
    file_buffer   = malloc(4 * 1024 * 1024);
    uint8_t *read_buffer = malloc(4 * 1024 * 1024);
    if (!image_storage || !file_buffer || !read_buffer) {
        fprintf(stderr, "Error: cannot allocate required buffers\n");
        free(image_storage); free(file_buffer); free(read_buffer);
        return 1;
    }

    /* --- safe open and read storage image --- */
    FILE *fptr = fopen(argv[3], "rb");
    if (fptr == NULL) {
        fprintf(stderr, "Error: cannot open storage image %s\n", argv[3]);
        perror("fopen");
        free(image_storage); free(file_buffer); free(read_buffer);
        return 1;
    }
    size_t got = fread(image_storage, 1, 4 * 1024 * 1024, fptr);
    if (got == 0) {
        fprintf(stderr, "Warning: read 0 bytes from %s (got=%zu)\n", argv[3], got);
    }
    fclose(fptr);

    /* Read target file (binary) */
    FILE *fptr_target = fopen(argv[1], "rb");
    size_t filesize = 0;
    if (fptr_target == NULL) {
        fprintf(stderr, "Error: cannot open target file %s\n", argv[1]);
        free(image_storage); free(file_buffer); free(read_buffer);
        return 1;
    } else {
        if (fseek(fptr_target, 0, SEEK_END) != 0) {
            perror("fseek");
            fclose(fptr_target);
            free(image_storage); free(file_buffer); free(read_buffer);
            return 1;
        }
        filesize = ftell(fptr_target);
        rewind(fptr_target);
        if (filesize > 0) {
            size_t r = fread(file_buffer, 1, filesize, fptr_target);
            if (r != (size_t)filesize) {
                fprintf(stderr, "Warning: fread read %zu of %zu bytes\n", r, filesize);
            }
        }
        fclose(fptr_target);
    }

    printf("Filename : %s\n",  argv[1]);
    printf("Filesize : %zu bytes\n", filesize);

    // EXT2 operations
    initialize_filesystem_ext2();

    char *name = argv[1];
    struct EXT2DriverRequest request;
    struct EXT2DriverRequest reqread;
    size_t filename_length = strlen(name);

    bool is_replace = false;
    printf("Filename       : %s\n", name);
    printf("Filename length: %zu\n", filename_length);

    /* prepare request (do NOT copy name into unknown memory) */
    request.buf = file_buffer;
    request.buffer_size = (uint32_t)filesize;
    request.name = name;
    request.name_len = (uint8_t)filename_length;
    request.is_directory = false;
    if (sscanf(argv[2], "%u", &request.parent_inode) != 1) {
        fprintf(stderr, "Error: invalid parent inode argument '%s'\n", argv[2]);
        free(image_storage); free(file_buffer); free(read_buffer);
        return 1;
    }

    /* read-check */
    reqread = request;
    reqread.buf = read_buffer;
    int retcode = read(reqread);
    if (retcode == 0) {
        is_replace = true;
        bool same = true;
        for (uint32_t i = 0; i < (uint32_t)filesize; i++) {
            if (read_buffer[i] != file_buffer[i]) {
                same = false;
                break;
            }
        }
        if (same) puts("same");
        else puts("not same");
    }

    retcode = write(&request);
    if (retcode == 1 && is_replace) {
        retcode = delete(request);
        retcode = write(&request);
    }

    if (retcode == 0) puts("Write success");
    else if (retcode == 1) puts("Error: File/folder name already exist");
    else if (retcode == 2) puts("Error: Invalid parent node index");
    else printf("Error: Unknown error (%d)\n", retcode);

    /* Write image in memory into original, overwrite them (binary mode) */
    fptr = fopen(argv[3], "wb");
    if (!fptr) {
        fprintf(stderr, "Error: cannot open storage image for writing %s\n", argv[3]);
        free(image_storage); free(file_buffer); free(read_buffer);
        return 1;
    }
    size_t written = fwrite(image_storage, 1, 4 * 1024 * 1024, fptr);
    if (written != 4 * 1024 * 1024) {
        fprintf(stderr, "Warning: fwrite wrote %zu bytes (expected %d)\n", written, 4 * 1024 * 1024);
    }
    fclose(fptr);

    free(image_storage);
    free(file_buffer);
    free(read_buffer);
    return 0;
}
