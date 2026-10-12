#ifndef MELO_IMAGE_H
#define MELO_IMAGE_H

#include "melo_codegen.h"
#include <stddef.h>
#include <stdint.h>

#define MELO_IMAGE_VERSION 1
#define MELO_IMAGE_MAX_CODE_WORDS (1u << 20)

typedef struct {
    MeloInstr *code;
    size_t code_len;
    int native_count;
    char native_names[MELO_MAX_NATIVES][MELO_NATIVE_NAME_LEN];
    int main_func_id;
} MeloImageInfo;

int melo_image_serialize(const MeloCompiledModule *mod,
                         const char **native_names, int native_count,
                         uint8_t **out_data, size_t *out_size,
                         char *err, size_t err_cap);

int melo_image_load(MeloVM *vm, const uint8_t *data, size_t size,
                    MeloImageInfo *info, char *err, size_t err_cap);

int melo_image_verify_natives(const MeloVM *vm, const MeloImageInfo *info,
                              char *err, size_t err_cap);

void melo_image_unload(MeloImageInfo *info);

int melo_image_write_file(const uint8_t *data, size_t size, const char *path);
int melo_image_read_file(const char *path, uint8_t **out_data, size_t *out_size);
int melo_image_write_c_array(const uint8_t *data, size_t size,
                             const char *path, const char *symbol);

#endif
