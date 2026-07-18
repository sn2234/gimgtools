#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#if defined(_MSC_VER) && _MSC_VER < 1600
#include "stdintvc.h"
#else
#include <stdint.h>
#endif

#include "util_indep.h"
#include "garmin_struct.h"

static int verbose = 0;
static int patch_count = 0;
static int skip_65001 = 0;
static int skip_other = 0;

static off_t fmul (unsigned int blk, unsigned int bsize)
{
    return (off_t)blk * (off_t)bsize;
}

static void process_lbl (FILE *fp, off_t lbl_offset)
{
    unsigned int codepage;
    off_t codepage_offset = lbl_offset + 0x0aa;
    
    codepage = read_2byte_at(fp, codepage_offset);
    printf("  LBL at 0x%llx, codepage = %d\n", (unsigned long long)lbl_offset, codepage);
    if (codepage == 65001) {
        unsigned char patch[2];
        patch[0] = 0xe4;
        patch[1] = 0x04;
        if (myfseek64(fp, codepage_offset)) {
            fprintf(stderr, "  seek failed to 0x%llx: %s\n",
                    (unsigned long long)codepage_offset, strerror(errno));
            return;
        }
        if (fwrite(patch, 2, 1, fp) != 1) {
            fprintf(stderr, "  write failed at 0x%llx: %s\n",
                    (unsigned long long)codepage_offset, strerror(errno));
            return;
        }
        printf("  => patched to 1252\n");
        patch_count++;
    } else if (codepage == 1252) {
        if (verbose)
            printf("  already 1252, skipping\n");
        skip_65001++;
    } else {
        printf("  codepage is %d, skipping\n", codepage);
        skip_other++;
    }
}

static int process_gmp (FILE *fp, off_t gmp_offset)
{
    struct garmin_gmp *gmp = (struct garmin_gmp *)malloc(sizeof(struct garmin_gmp));
    if (gmp == NULL) {
        fprintf(stderr, "Failed to allocate GMP header\n");
        return 1;
    }
    
    if (myfseek64(fp, gmp_offset)) {
        fprintf(stderr, "  GMP seek to 0x%llx failed: %s\n",
                (unsigned long long)gmp_offset, strerror(errno));
        free(gmp);
        return 1;
    }
    if (fread(gmp, sizeof(struct garmin_gmp), 1, fp) != 1) {
        fprintf(stderr, "  GMP read at 0x%llx failed: %s\n",
                (unsigned long long)gmp_offset, strerror(errno));
        free(gmp);
        return 1;
    }

    printf("  GMP hlen=%u type=%.10s locked=%u lbl_off=0x%x tre_off=0x%x rgn_off=0x%x sizeof(GMP)=%zu\n",
           gmp->comm.hlen, gmp->comm.type, gmp->comm.locked,
           gmp->lbl_offset, gmp->tre_offset, gmp->rgn_offset,
           sizeof(struct garmin_gmp));
    
    if (memcmp(gmp->comm.type + 7, "GMP", 3) != 0) {
        fprintf(stderr, "  Invalid GMP subfile header at 0x%llx: '%.10s'\n",
                (unsigned long long)gmp_offset, gmp->comm.type);
        free(gmp);
        return 0;
    }
    
    if (gmp->lbl_offset != 0) {
        off_t lbl_offset = gmp_offset + gmp->lbl_offset;
        if (verbose) {
            printf("  LBL inside GMP at 0x%llx\n", (unsigned long long)lbl_offset);
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
    unsigned int block_size, fatstart, fatend;
    unsigned int i;
    
    fp = fopen(path, "rb+");
    if (fp == NULL) {
        fprintf(stderr, "Cannot open %s for reading/writing\n", path);
        return 1;
    }
    
    img_header = (struct garmin_img *)malloc(sizeof(struct garmin_img));
    if (img_header == NULL) {
        fprintf(stderr, "Failed to allocate image header\n");
        fclose(fp);
        return 1;
    }
    
    if (fread(img_header, sizeof(struct garmin_img), 1, fp) != 1) {
        fprintf(stderr, "Failed to read image header: %s\n", strerror(errno));
        free(img_header);
        fclose(fp);
        return 1;
    }
    
    if (memcmp(img_header->signature, "DSKIMG", 6) != 0) {
        fprintf(stderr, "Not a valid Garmin IMG file\n");
        free(img_header);
        fclose(fp);
        return 1;
    }
    
    if (img_header->xor_byte != 0) {
        fprintf(stderr, "XOR is not 0. Fix it first with gimgxor.\n");
        free(img_header);
        fclose(fp);
        return 1;
    }
    
    block_size = 1u << (img_header->blockexp1 + img_header->blockexp2);
    fatstart = img_header->fat_offset == 0 ? 3 : img_header->fat_offset;
    
    if (img_header->data_offset == 0) {
        struct garmin_fat *fat = (struct garmin_fat *)malloc(sizeof(struct garmin_fat));
        if (fat == NULL) {
            fprintf(stderr, "Failed to allocate FAT entry\n");
            free(img_header);
            fclose(fp);
            return 1;
        }
        if (myfseek64(fp, (off_t)fatstart * 512)) {
            fprintf(stderr, "rootdir seek to 0x%llx failed: %s\n",
                    (unsigned long long)fatstart * 512, strerror(errno));
            free(fat);
            free(img_header);
            fclose(fp);
            return 1;
        }
        if (fread(fat, sizeof(struct garmin_fat), 1, fp) != 1) {
            fprintf(stderr, "rootdir read failed: %s\n", strerror(errno));
            free(fat);
            free(img_header);
            fclose(fp);
            return 1;
        }
        if (fat->flag != 1 ||
            memcmp(fat->name, "        ", 8) != 0 ||
            memcmp(fat->type, "   ", 3) != 0) {
            fprintf(stderr, "imgheader.data_offset = 0 but the first file is not root dir!\n");
            fprintf(stderr, "  flag=%02x name='%.8s' type='%.3s'\n",
                    fat->flag, fat->name, fat->type);
            free(fat);
            free(img_header);
            fclose(fp);
            return 1;
        }
        fatstart++;
        if (fat->size % 512 != 0 || fat->size <= (unsigned)fatstart * 512) {
            fprintf(stderr, "rootdir.size = %x which is bad\n", fat->size);
            free(fat);
            free(img_header);
            fclose(fp);
            return 1;
        }
        fatend = fat->size / 512;
        printf("block_size=%u, fatstart=%u, fatend=%u (rootdir size=%u)\n",
               block_size, fatstart, fatend, fat->size);
        free(fat);
    } else {
        fatend = img_header->data_offset / 512;
        printf("block_size=%u, fatstart=%u, fatend=%u (data_offset=%u)\n",
               block_size, fatstart, fatend, img_header->data_offset);
    }
    
    for (i = fatstart; i < fatend; i++) {
        struct garmin_fat *fat = (struct garmin_fat *)malloc(sizeof(struct garmin_fat));
        off_t offset = (off_t)i * 512;
        char subfile_name[16];
        int matched = 0;
        
        if (fat == NULL) {
            fprintf(stderr, "Failed to allocate FAT entry\n");
            continue;
        }
        
        if (myfseek64(fp, offset)) {
            fprintf(stderr, "fat[%u] seek to 0x%llx failed: %s\n",
                    i, (unsigned long long)offset, strerror(errno));
            free(fat);
            continue;
        }
        if (fread(fat, sizeof(struct garmin_fat), 1, fp) != 1) {
            fprintf(stderr, "fat[%u] read at 0x%llx failed: %s\n",
                    i, (unsigned long long)offset, strerror(errno));
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
        
        memcpy(subfile_name, fat->name, 8);
        subfile_name[8] = '.';
        memcpy(subfile_name + 9, fat->type, 3);
        subfile_name[12] = '\0';
        
        printf("fat[%u] %s part=%u size=%u blocks0=%u\n",
               i, subfile_name, fat->part, fat->size, fat->blocks[0]);
        
        if (memcmp(fat->type, "GMP", 3) == 0) {
            off_t gmp_offset = fmul(fat->blocks[0], block_size);
            printf("  GMP at 0x%llx\n", (unsigned long long)gmp_offset);
            process_gmp(fp, gmp_offset);
            matched = 1;
        }
        else if (memcmp(fat->type, "LBL", 3) == 0) {
            off_t lbl_offset = fmul(fat->blocks[0], block_size);
            printf("  LBL at 0x%llx\n", (unsigned long long)lbl_offset);
            process_lbl(fp, lbl_offset);
            matched = 1;
        }
        
        if (!matched) {
            printf("  (skipped, not GMP or LBL)\n");
        }
        
        free(fat);
    }
    
    free(img_header);
    fclose(fp);

    printf("Done: %d patched, %d unchanged (1252), %d skipped (other codepage)\n",
           patch_count, skip_65001, skip_other);
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
