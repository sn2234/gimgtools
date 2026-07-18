#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "util_indep.h"
#include "garmin_struct.h"

static int verbose = 0;

static void process_lbl (FILE *fp, off_t lbl_offset)
{
    unsigned int codepage;
    off_t codepage_offset = lbl_offset + 0x0aa;
    
    codepage = read_2byte_at(fp, codepage_offset);
    if (verbose) {
        printf("  LBL at 0x%lx, codepage = %d\n", (unsigned long)lbl_offset, codepage);
    }
    if (codepage == 65001) {
        // patch to 1252
        unsigned char patch[2];
        patch[0] = 0x54; // 1252 = 0x04E4, little-endian 0xE4 0x04
        patch[1] = 0x04;
        if (myfseek64(fp, codepage_offset)) {
            perror(NULL);
            return;
        }
        if (fwrite(patch, 2, 1, fp) != 1) {
            perror(NULL);
            return;
        }
        printf("  Patched codepage from 65001 to 1252\n");
    } else {
        if (verbose) {
            printf("  Codepage is not 65001, skipping\n");
        }
    }
}

static int process_gmp (FILE *fp, off_t gmp_offset)
{
    struct garmin_gmp *gmp = (struct garmin_gmp *)malloc(sizeof(struct garmin_gmp));
    if (gmp == NULL) {
        fprintf(stderr, "Failed to allocate GMP header\n");
        return 1;
    }
    
    // read GMP header
    if (myfseek64(fp, gmp_offset)) {
        perror(NULL);
        free(gmp);
        return 1;
    }
    if (fread(gmp, sizeof(struct garmin_gmp), 1, fp) != 1) {
        perror(NULL);
        free(gmp);
        return 1;
    }
    
    // check if it's a valid GMP header
    if (memcmp(gmp->comm.type, "GMP", 3) != 0) {
        if (verbose) {
            printf("  Not a valid GMP header\n");
        }
        free(gmp);
        return 0;
    }
    
    // process LBL subfile if present
    if (gmp->lbl_offset != 0) {
        off_t lbl_offset = gmp_offset + gmp->lbl_offset;
        if (verbose) {
            printf("  LBL inside GMP at 0x%lx\n", (unsigned long)lbl_offset);
        }
        process_lbl(fp, lbl_offset);
    }
    
    free(gmp);
    return 0;
}

static int process_img (const char *path)
{
    FILE *fp;
    struct garmin_img *img_header;
    int block_size, fatstart, fatend;
    int i;
    
    fp = fopen(path, "rb+");
    if (fp == NULL) {
        fprintf(stderr, "Cannot open %s for reading/writing\n", path);
        return 1;
    }
    
    // read image header
    img_header = (struct garmin_img *)malloc(sizeof(struct garmin_img));
    if (img_header == NULL) {
        fprintf(stderr, "Failed to allocate image header\n");
        fclose(fp);
        return 1;
    }
    
    if (fread(img_header, sizeof(struct garmin_img), 1, fp) != 1) {
        perror(NULL);
        free(img_header);
        fclose(fp);
        return 1;
    }
    
    // check signature
    if (memcmp(img_header->signature, "DSKIMG", 6) != 0) {
        fprintf(stderr, "Not a valid Garmin IMG file\n");
        free(img_header);
        fclose(fp);
        return 1;
    }
    
    // check XOR
    if (img_header->xor_byte != 0) {
        fprintf(stderr, "XOR is not 0. Fix it first with gimgxor.\n");
        free(img_header);
        fclose(fp);
        return 1;
    }
    
    block_size = 1 << (img_header->blockexp1 + img_header->blockexp2);
    fatstart = img_header->fat_offset == 0 ? 3 : img_header->fat_offset;
    
    if (img_header->data_offset == 0) {
        // use root dir
        struct garmin_fat *fat = (struct garmin_fat *)malloc(sizeof(struct garmin_fat));
        if (fat == NULL) {
            fprintf(stderr, "Failed to allocate FAT entry\n");
            free(img_header);
            fclose(fp);
            return 1;
        }
        if (myfseek64(fp, fatstart * 512)) {
            perror(NULL);
            free(fat);
            free(img_header);
            fclose(fp);
            return 1;
        }
        if (fread(fat, sizeof(struct garmin_fat), 1, fp) != 1) {
            perror(NULL);
            free(fat);
            free(img_header);
            fclose(fp);
            return 1;
        }
        if (fat->flag != 1 ||
            memcmp(fat->name, "        ", 8) != 0 ||
            memcmp(fat->type, "   ", 3) != 0) {
            fprintf(stderr, "imgheader.data_offset = 0 but the first file is not root dir!\n");
            free(fat);
            free(img_header);
            fclose(fp);
            return 1;
        }
        fatstart++;
        if (fat->size % 512 != 0 || fat->size <= fatstart * 512) {
            fprintf(stderr, "rootdir.size = %x which is bad\n", fat->size);
            free(fat);
            free(img_header);
            fclose(fp);
            return 1;
        }
        fatend = fat->size / 512;
        if (verbose) {
            printf("Parsing fat use rootdir, fatstart=%d, fatend=%d\n", fatstart, fatend);
        }
        free(fat);
    } else {
        fatend = img_header->data_offset / 512;
        if (verbose) {
            printf("Parsing fat use data_offset, fatstart=%d, fatend=%d\n", fatstart, fatend);
        }
    }
    
    // iterate over FAT entries
    for (i = fatstart; i < fatend; i++) {
        struct garmin_fat *fat = (struct garmin_fat *)malloc(sizeof(struct garmin_fat));
        off_t offset = i * 512;
        char subfile_name[16];
        
        if (fat == NULL) {
            fprintf(stderr, "Failed to allocate FAT entry\n");
            continue;
        }
        
        if (myfseek64(fp, offset)) {
            perror(NULL);
            free(fat);
            continue;
        }
        if (fread(fat, sizeof(struct garmin_fat), 1, fp) != 1) {
            perror(NULL);
            free(fat);
            continue;
        }
        
        if (fat->flag != 1) {
            free(fat);
            continue;
        }
        if (memcmp(fat->name, "        ", 8) == 0) {
            free(fat);
            continue;
        }
        
        // construct subfile name
        memcpy(subfile_name, fat->name, 8);
        subfile_name[8] = '.';
        memcpy(subfile_name + 9, fat->type, 3);
        subfile_name[12] = '\0';
        
        if (verbose) {
            printf("Processing FAT entry %d: %s\n", i, subfile_name);
        }
        
        // check if it's a GMP file
        if (memcmp(fat->type, "GMP", 3) == 0) {
            off_t gmp_offset = fat->blocks[0] * block_size;
            if (verbose) {
                printf("  GMP at 0x%lx\n", (unsigned long)gmp_offset);
            }
            process_gmp(fp, gmp_offset);
        }
        // check if it's a standalone LBL file
        else if (memcmp(fat->type, "LBL", 3) == 0) {
            off_t lbl_offset = fat->blocks[0] * block_size;
            if (verbose) {
                printf("  Standalone LBL at 0x%lx\n", (unsigned long)lbl_offset);
            }
            process_lbl(fp, lbl_offset);
        }
        // for other types, we could optionally check inside GMP if present
        // but we already handled GMP above
        
        free(fat);
    }
    
    free(img_header);
    fclose(fp);
    return 0;
}

int main (int argc, char *argv[])
{
    int i;
    const char *img_path = NULL;
    
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-?") == 0) {
            printf("Usage: %s [-v] [-h] file.img\n", argv[0]);
            printf("Change codepage from 65001 (UTF-8) to 1252 (Latin-1) in LBL subfiles.\n");
            printf("Options:\n");
            printf("  -v      verbose output\n");
            printf("  -h      show this help\n");
            return 0;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option %s\n", argv[i]);
            printf("Usage: %s [-v] [-h] file.img\n", argv[0]);
            return 1;
        } else {
            if (img_path == NULL) {
                img_path = argv[i];
            } else {
                fprintf(stderr, "Only one image file allowed.\n");
                printf("Usage: %s [-v] [-h] file.img\n", argv[0]);
                return 1;
            }
        }
    }
    
    if (img_path == NULL) {
        fprintf(stderr, "No image file specified.\n");
        printf("Usage: %s [-v] [-h] file.img\n", argv[0]);
        return 1;
    }
    
    printf("Processing %s\n", img_path);
    return process_img(img_path);
}
