#include "melo_stdlib_gs.h"
#include "melo_value.h"
#include <string.h>

#ifdef MELO_PS2_TARGET
#include <gsKit.h>
#include <dmaKit.h>
#include <gsInline.h>

static GSGLOBAL *gs_global = NULL;

int melo_gs_backend_init(void) {
    gs_global = gsKit_init_global();
    gs_global->Mode = GS_MODE_NTSC;
    gs_global->Interlace = GS_INTERLACED;
    gs_global->Field = GS_FIELD;
    gs_global->PSM = GS_PSM_CT24;
    gs_global->PSMZ = GS_PSMZ_16S;
    gs_global->DoubleBuffering = GS_SETTING_ON;
    gs_global->ZBuffering = GS_SETTING_OFF;
    gs_global->PrimAlphaEnable = GS_SETTING_ON;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(gs_global);
    gsKit_mode_switch(gs_global, GS_ONESHOT);
    gsKit_clear(gs_global, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));
    return 0;
}

void melo_gs_backend_frame_begin(void) {
    gsKit_queue_reset(gs_global->Per_Queue);
}

void melo_gs_backend_frame_end(void) {
    gsKit_queue_exec(gs_global);
    gsKit_finish();
    gsKit_flip(gs_global);
    gsKit_clear(gs_global, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));
}

void melo_gs_backend_draw_sprite(uint32_t vram_offset, uint32_t tex_w, uint32_t tex_h,
                                  float x, float y, float w, float h) {
    GSTEXTURE tex;
    memset(&tex, 0, sizeof(tex));
    tex.Width = tex_w;
    tex.Height = tex_h;
    tex.PSM = GS_PSM_CT32;
    tex.Vram = vram_offset;
    tex.VramClut = 0;

    gsKit_prim_sprite_texture(gs_global, &tex,
                               x, y, 0.0f, 0.0f,
                               x + w, y + h, (float)tex_w, (float)tex_h,
                               1, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
}

#else
#include <stdio.h>

int melo_gs_backend_init(void) {
    printf("[gs] init (stub)\n");
    return 0;
}

void melo_gs_backend_frame_begin(void) {
}

void melo_gs_backend_frame_end(void) {
    printf("[gs] frame end (stub)\n");
}

void melo_gs_backend_draw_sprite(uint32_t vram_offset, uint32_t tex_w, uint32_t tex_h,
                                  float x, float y, float w, float h) {
    printf("[gs] draw_sprite vram_offset=%u tex=%ux%u at (%.1f,%.1f) size (%.1f,%.1f)\n",
           vram_offset, tex_w, tex_h, x, y, w, h);
}

#endif

static int native_gs_init(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    int rc = melo_gs_backend_init();
    *out_result = melo_int(rc);
    return 0;
}

static int native_gs_frame_begin(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    melo_gs_backend_frame_begin();
    *out_result = melo_nil();
    return 0;
}

static int native_gs_frame_end(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    melo_gs_backend_frame_end();
    *out_result = melo_nil();
    return 0;
}

static int native_gs_draw_sprite(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 5 || args[0].type != MELO_VRAM_TEX || args[0].as.vram == NULL) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "gs_draw_sprite(tex: vram_tex, x: float, y: float, w: float, h: float) requires a @vram texture and four float arguments");
        return -1;
    }
    for (int i = 1; i < 5; i++) {
        if (args[i].type != MELO_FLOAT) {
            vm->error = 1;
            snprintf(vm->error_msg, sizeof(vm->error_msg),
                     "gs_draw_sprite: x, y, w, h must be float");
            return -1;
        }
    }

    MeloVramTex *tex = args[0].as.vram;
    if (tex->status != MELO_VRAM_RESIDENT) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "gs_draw_sprite: texture is not resident, call upload(tex) first");
        return -1;
    }

    melo_gs_backend_draw_sprite(tex->vram_offset, tex->width, tex->height,
                                 args[1].as.f32, args[2].as.f32, args[3].as.f32, args[4].as.f32);
    *out_result = melo_nil();
    return 0;
}

int melo_stdlib_register_gs(MeloVM *vm) {
    static const struct {
        const char *name;
        MeloNativeFn fn;
    } entries[] = {
        { "gs_init",         native_gs_init },
        { "gs_frame_begin",  native_gs_frame_begin },
        { "gs_frame_end",    native_gs_frame_end },
        { "gs_draw_sprite",  native_gs_draw_sprite },
    };
    int count = (int)(sizeof(entries) / sizeof(entries[0]));
    for (int i = 0; i < count; i++) {
        if (melo_vm_register_native(vm, entries[i].name, entries[i].fn) < 0) {
            return -1;
        }
    }
    return 0;
}

const char **melo_stdlib_gs_native_names(int *out_count) {
    static const char *names[] = {
        "gs_init", "gs_frame_begin", "gs_frame_end", "gs_draw_sprite"
    };
    *out_count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}
