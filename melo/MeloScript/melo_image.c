#include "melo_image.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_MAGIC_0 'M'
#define IMAGE_MAGIC_1 'E'
#define IMAGE_MAGIC_2 'L'
#define IMAGE_MAGIC_3 'O'
#define IMAGE_NO_ENTRY 0xFFFFFFFFu
#define IMAGE_FUNC_VUKERNEL 1u
#define IMAGE_FUNC_HOT 2u
#define IMAGE_HEADER_SIZE 32u

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    int failed;
} Writer;

typedef struct {
    const uint8_t *p;
    size_t size;
    size_t pos;
    int failed;
} Reader;

static void set_err(char *err, size_t cap, const char *fmt, ...) {
    if (!err || cap == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

static uint32_t fnv32(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void w_init(Writer *w) {
    w->cap = 4096;
    w->len = 0;
    w->failed = 0;
    w->data = (uint8_t *)malloc(w->cap);
    if (!w->data) w->failed = 1;
}

static void w_bytes(Writer *w, const void *src, size_t n) {
    if (w->failed) return;
    if (w->len + n > w->cap) {
        size_t cap = w->cap;
        while (w->len + n > cap) cap *= 2;
        uint8_t *grown = (uint8_t *)realloc(w->data, cap);
        if (!grown) {
            w->failed = 1;
            return;
        }
        w->data = grown;
        w->cap = cap;
    }
    memcpy(w->data + w->len, src, n);
    w->len += n;
}

static void w_u8(Writer *w, uint8_t v) {
    w_bytes(w, &v, 1);
}

static void w_u16(Writer *w, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
    w_bytes(w, b, 2);
}

static void w_u32(Writer *w, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
        (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF)
    };
    w_bytes(w, b, 4);
}

static void w_f32(Writer *w, float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    w_u32(w, bits);
}

static void w_fixed(Writer *w, const char *s, size_t width) {
    uint8_t buf[64];
    size_t n = width < sizeof(buf) ? width : sizeof(buf);
    memset(buf, 0, sizeof(buf));
    size_t slen = strlen(s);
    if (slen >= n) slen = n - 1;
    memcpy(buf, s, slen);
    w_bytes(w, buf, n);
}

static int r_need(Reader *r, size_t n) {
    if (r->failed || r->size - r->pos < n) {
        r->failed = 1;
        return 0;
    }
    return 1;
}

static uint8_t r_u8(Reader *r) {
    if (!r_need(r, 1)) return 0;
    return r->p[r->pos++];
}

static uint16_t r_u16(Reader *r) {
    if (!r_need(r, 2)) return 0;
    uint16_t v = (uint16_t)(r->p[r->pos] | (r->p[r->pos + 1] << 8));
    r->pos += 2;
    return v;
}

static uint32_t r_u32(Reader *r) {
    if (!r_need(r, 4)) return 0;
    const uint8_t *b = r->p + r->pos;
    uint32_t v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                 ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    r->pos += 4;
    return v;
}

static float r_f32(Reader *r) {
    uint32_t bits = r_u32(r);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static const uint8_t *r_bytes(Reader *r, size_t n) {
    if (!r_need(r, n)) return NULL;
    const uint8_t *p = r->p + r->pos;
    r->pos += n;
    return p;
}

int melo_image_serialize(const MeloCompiledModule *mod,
                         const char **native_names, int native_count,
                         uint8_t **out_data, size_t *out_size,
                         char *err, size_t err_cap) {
    if (!mod || !out_data || !out_size) {
        set_err(err, err_cap, "invalid arguments");
        return -1;
    }
    if (native_count < 0 || native_count > MELO_MAX_NATIVES ||
        (native_count > 0 && !native_names)) {
        set_err(err, err_cap, "invalid native name list");
        return -1;
    }
    if (mod->code_count <= 0 || (uint32_t)mod->code_count > MELO_IMAGE_MAX_CODE_WORDS) {
        set_err(err, err_cap, "code size out of range");
        return -1;
    }

    Writer w;
    w_init(&w);

    w_u8(&w, IMAGE_MAGIC_0);
    w_u8(&w, IMAGE_MAGIC_1);
    w_u8(&w, IMAGE_MAGIC_2);
    w_u8(&w, IMAGE_MAGIC_3);
    w_u16(&w, MELO_IMAGE_VERSION);
    w_u16(&w, 0);
    w_u32(&w, (uint32_t)mod->code_count);
    w_u32(&w, (uint32_t)mod->const_count);
    w_u32(&w, (uint32_t)mod->func_count);
    w_u32(&w, (uint32_t)mod->struct_count);
    w_u32(&w, (uint32_t)native_count);
    w_u32(&w, (uint32_t)mod->main_func_id);

    for (int i = 0; i < mod->code_count; i++) {
        w_u32(&w, mod->code[i]);
    }

    for (int i = 0; i < mod->const_count; i++) {
        const MeloValue *v = &mod->consts[i];
        w_u8(&w, (uint8_t)v->type);
        switch (v->type) {
            case MELO_NIL:
                break;
            case MELO_INT32:
                w_u32(&w, (uint32_t)v->as.i32);
                break;
            case MELO_FLOAT:
                w_f32(&w, v->as.f32);
                break;
            case MELO_VEC2:
                w_f32(&w, v->as.v2.x);
                w_f32(&w, v->as.v2.y);
                break;
            case MELO_VEC3:
                w_f32(&w, v->as.v3.x);
                w_f32(&w, v->as.v3.y);
                w_f32(&w, v->as.v3.z);
                break;
            case MELO_VEC4:
                w_f32(&w, v->as.v4.x);
                w_f32(&w, v->as.v4.y);
                w_f32(&w, v->as.v4.z);
                w_f32(&w, v->as.v4.w);
                break;
            case MELO_STRING:
                w_u32(&w, v->as.str->length);
                w_bytes(&w, v->as.str->data, v->as.str->length);
                break;
            default:
                free(w.data);
                set_err(err, err_cap, "constant %d has a type that cannot be stored in an image", i);
                return -1;
        }
    }

    for (int i = 0; i < mod->func_count; i++) {
        const MeloFuncInfo *f = &mod->funcs[i];
        uint32_t entry = IMAGE_NO_ENTRY;
        if (f->entry && f->entry >= mod->code && f->entry < mod->code + mod->code_count) {
            entry = (uint32_t)(f->entry - mod->code);
        }
        uint32_t flags = 0;
        if (f->is_vukernel) flags |= IMAGE_FUNC_VUKERNEL;
        if (f->is_hot) flags |= IMAGE_FUNC_HOT;
        w_fixed(&w, f->name, MELO_FUNC_NAME_LEN);
        w_u32(&w, entry);
        w_u32(&w, (uint32_t)f->num_params);
        w_u32(&w, (uint32_t)f->num_regs);
        w_u32(&w, flags);
    }

    for (int i = 0; i < mod->struct_count; i++) {
        const MeloStructInfo *s = &mod->structs[i];
        w_u32(&w, (uint32_t)s->field_count);
        w_u32(&w, (uint32_t)s->is_soa);
        for (int k = 0; k < MELO_MAX_STRUCT_FIELDS; k++) {
            uint8_t buf[32];
            memcpy(buf, s->field_names[k], sizeof(buf));
            buf[31] = 0;
            w_bytes(&w, buf, sizeof(buf));
        }
    }

    for (int i = 0; i < native_count; i++) {
        w_fixed(&w, native_names[i], MELO_NATIVE_NAME_LEN);
    }

    if (w.failed) {
        free(w.data);
        set_err(err, err_cap, "out of memory");
        return -1;
    }

    w_u32(&w, fnv32(w.data, w.len));
    if (w.failed) {
        free(w.data);
        set_err(err, err_cap, "out of memory");
        return -1;
    }

    *out_data = w.data;
    *out_size = w.len;
    return 0;
}

static void fixed_to_string(char *dst, size_t dst_cap, const uint8_t *src, size_t src_len) {
    size_t n = src_len < dst_cap - 1 ? src_len : dst_cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int melo_image_load(MeloVM *vm, const uint8_t *data, size_t size,
                    MeloImageInfo *info, char *err, size_t err_cap) {
    if (!vm || !data || !info) {
        set_err(err, err_cap, "invalid arguments");
        return -1;
    }
    memset(info, 0, sizeof(*info));
    info->main_func_id = -1;

    if (size < IMAGE_HEADER_SIZE + 4) {
        set_err(err, err_cap, "image is too small");
        return -1;
    }
    if (data[0] != IMAGE_MAGIC_0 || data[1] != IMAGE_MAGIC_1 ||
        data[2] != IMAGE_MAGIC_2 || data[3] != IMAGE_MAGIC_3) {
        set_err(err, err_cap, "bad image magic");
        return -1;
    }

    Reader tail = { data, size, size - 4, 0 };
    uint32_t stored = r_u32(&tail);
    if (stored != fnv32(data, size - 4)) {
        set_err(err, err_cap, "image checksum mismatch");
        return -1;
    }

    Reader r = { data, size - 4, 4, 0 };
    uint16_t version = r_u16(&r);
    r_u16(&r);
    uint32_t code_count = r_u32(&r);
    uint32_t const_count = r_u32(&r);
    uint32_t func_count = r_u32(&r);
    uint32_t struct_count = r_u32(&r);
    uint32_t native_count = r_u32(&r);
    int32_t main_id = (int32_t)r_u32(&r);

    if (version != MELO_IMAGE_VERSION) {
        set_err(err, err_cap, "unsupported image version %u", (unsigned)version);
        return -1;
    }
    if (code_count == 0 || code_count > MELO_IMAGE_MAX_CODE_WORDS ||
        const_count > MELO_MAX_CONSTS || func_count > MELO_MAX_FUNCS ||
        struct_count > MELO_MAX_STRUCTS || native_count > MELO_MAX_NATIVES) {
        set_err(err, err_cap, "image section counts are out of range");
        return -1;
    }
    if (main_id < -1 || main_id >= (int32_t)func_count) {
        set_err(err, err_cap, "main function id is out of range");
        return -1;
    }

    MeloInstr *code = (MeloInstr *)malloc((size_t)code_count * sizeof(MeloInstr));
    if (!code) {
        set_err(err, err_cap, "out of memory");
        return -1;
    }
    for (uint32_t i = 0; i < code_count; i++) {
        code[i] = r_u32(&r);
    }
    if (r.failed) {
        free(code);
        set_err(err, err_cap, "image is truncated in the code section");
        return -1;
    }

    melo_vm_init(vm, code, code_count);

    for (uint32_t i = 0; i < const_count && !r.failed; i++) {
        uint8_t type = r_u8(&r);
        MeloValue v = melo_nil();
        switch (type) {
            case MELO_NIL:
                break;
            case MELO_INT32:
                v = melo_int((int32_t)r_u32(&r));
                break;
            case MELO_FLOAT:
                v = melo_float(r_f32(&r));
                break;
            case MELO_VEC2:
                v.type = MELO_VEC2;
                v.as.v2.x = r_f32(&r);
                v.as.v2.y = r_f32(&r);
                break;
            case MELO_VEC3:
                v.type = MELO_VEC3;
                v.as.v3.x = r_f32(&r);
                v.as.v3.y = r_f32(&r);
                v.as.v3.z = r_f32(&r);
                break;
            case MELO_VEC4: {
                float x = r_f32(&r);
                float y = r_f32(&r);
                float z = r_f32(&r);
                float wv = r_f32(&r);
                v = melo_vec4(x, y, z, wv);
                break;
            }
            case MELO_STRING: {
                uint32_t len = r_u32(&r);
                if (len > MELO_STRPOOL_SIZE) {
                    r.failed = 1;
                    break;
                }
                const uint8_t *bytes = r_bytes(&r, len);
                if (!bytes) break;
                MeloString *s = melo_strpool_intern(&vm->strpool, (const char *)bytes, len);
                if (!s) {
                    free(code);
                    set_err(err, err_cap, "string pool exhausted while loading constants");
                    return -1;
                }
                v = melo_string(s);
                break;
            }
            default:
                r.failed = 1;
                break;
        }
        vm->consts[i] = v;
    }
    vm->const_count = const_count;

    for (uint32_t i = 0; i < func_count && !r.failed; i++) {
        MeloFuncInfo *f = &vm->funcs[i];
        const uint8_t *name = r_bytes(&r, MELO_FUNC_NAME_LEN);
        uint32_t entry = r_u32(&r);
        uint32_t num_params = r_u32(&r);
        uint32_t num_regs = r_u32(&r);
        uint32_t flags = r_u32(&r);
        if (r.failed) break;
        fixed_to_string(f->name, MELO_FUNC_NAME_LEN, name, MELO_FUNC_NAME_LEN);
        if (entry == IMAGE_NO_ENTRY) {
            f->entry = NULL;
        } else if (entry < code_count) {
            f->entry = code + entry;
        } else {
            r.failed = 1;
            break;
        }
        f->num_params = (int)num_params;
        f->num_regs = (int)num_regs;
        f->is_vukernel = (flags & IMAGE_FUNC_VUKERNEL) ? 1 : 0;
        f->is_hot = (flags & IMAGE_FUNC_HOT) ? 1 : 0;
        f->hot_call_count = 0;
        f->aot_fn = NULL;
    }
    vm->func_count = (int)func_count;

    for (uint32_t i = 0; i < struct_count && !r.failed; i++) {
        MeloStructInfo *s = &vm->structs[i];
        uint32_t fields = r_u32(&r);
        uint32_t soa = r_u32(&r);
        if (fields > MELO_MAX_STRUCT_FIELDS) {
            r.failed = 1;
            break;
        }
        s->field_count = (int)fields;
        s->is_soa = (int)soa;
        for (int k = 0; k < MELO_MAX_STRUCT_FIELDS; k++) {
            const uint8_t *nm = r_bytes(&r, 32);
            if (!nm) break;
            fixed_to_string(s->field_names[k], 32, nm, 32);
        }
    }
    vm->struct_count = (int)struct_count;

    for (uint32_t i = 0; i < native_count && !r.failed; i++) {
        const uint8_t *nm = r_bytes(&r, MELO_NATIVE_NAME_LEN);
        if (!nm) break;
        fixed_to_string(info->native_names[i], MELO_NATIVE_NAME_LEN, nm, MELO_NATIVE_NAME_LEN);
    }
    info->native_count = (int)native_count;

    if (r.failed) {
        free(code);
        set_err(err, err_cap, "image is malformed or truncated");
        return -1;
    }
    if (r.pos != r.size) {
        free(code);
        set_err(err, err_cap, "image has unexpected trailing data");
        return -1;
    }

    info->code = code;
    info->code_len = code_count;
    info->main_func_id = main_id;

    if (main_id >= 0 && vm->funcs[main_id].entry) {
        vm->code = vm->funcs[main_id].entry;
    }
    return 0;
}

int melo_image_verify_natives(const MeloVM *vm, const MeloImageInfo *info,
                              char *err, size_t err_cap) {
    if (!vm || !info) {
        set_err(err, err_cap, "invalid arguments");
        return -1;
    }
    if (vm->native_count < info->native_count) {
        set_err(err, err_cap, "image needs %d natives but only %d are registered",
                info->native_count, vm->native_count);
        return -1;
    }
    for (int i = 0; i < info->native_count; i++) {
        if (strcmp(vm->natives[i].name, info->native_names[i]) != 0) {
            set_err(err, err_cap, "native %d mismatch: image expects '%s' but VM has '%s'",
                    i, info->native_names[i], vm->natives[i].name);
            return -1;
        }
    }
    return 0;
}

void melo_image_unload(MeloImageInfo *info) {
    if (!info) return;
    free(info->code);
    info->code = NULL;
    info->code_len = 0;
}

int melo_image_write_file(const uint8_t *data, size_t size, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(data, 1, size, f);
    int rc = fclose(f);
    return (written == size && rc == 0) ? 0 : -1;
}

int melo_image_read_file(const char *path, uint8_t **out_data, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return -1;
    }
    *out_data = buf;
    *out_size = (size_t)n;
    return 0;
}

int melo_image_write_c_array(const uint8_t *data, size_t size,
                             const char *path, const char *symbol) {
    if (!symbol || !*symbol) return -1;
    for (const char *c = symbol; *c; c++) {
        int ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_' ||
                 (c != symbol && *c >= '0' && *c <= '9');
        if (!ok) return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "const unsigned char %s[] __attribute__((aligned(16))) = {\n", symbol);
    for (size_t i = 0; i < size; i++) {
        fprintf(f, "%s0x%02x,", (i % 12 == 0) ? "    " : " ", data[i]);
        if (i % 12 == 11 || i + 1 == size) fputc('\n', f);
    }
    fprintf(f, "};\n");
    fprintf(f, "const unsigned int %s_len = %zu;\n", symbol, size);
    int rc = fclose(f);
    return rc == 0 ? 0 : -1;
}
