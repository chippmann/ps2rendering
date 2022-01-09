
#ifndef RENDERTEST_TEXTURELOADER_H
#define RENDERTEST_TEXTURELOADER_H

#include <tamtypes.h>
#include <gs_psm.h>
#include <draw_sampling.h>
#include <draw_buffers.h>
#include <draw2d.h>

struct Texture {
    u8 format{GS_PSM_32};
    float width;
    float height;
    const unsigned char* data;
    texrect_t texture_rect{
            .v0 = {
//                    .x = p_pos_x,
//                    .y = p_pos_y,
//                    .z = 0
            },
            .t0  = {
                    .s = 0,
                    .t = 0
            },
            .v1 = {
//                    .x = p_pos_x + texture->width,
//                    .y = p_pos_y + texture->height,
//                    .z = 0
            },
            .t1  = {
//                    .s = texture->width,
//                    .t = texture->height
            },
            .color = {
                    .r = 0x80,
                    .g = 0x80,
                    .b = 0x80,
                    .a = 0x80,
                    .q = 1.0f
            }
    };
    texwrap_t wrap_settings {
            .horizontal = WRAP_REPEAT,
            .vertical = WRAP_REPEAT,
            .minu = 0,
            .maxu = 0,
            .minv = 0,
            .maxv = 0,
    };
    clutbuffer_t clut_buffer {
            .address = 0,
            .psm = 0,
            .storage_mode = CLUT_STORAGE_MODE1,
            .start = 0,
            .load_method = CLUT_NO_LOAD,
    };
    texbuffer_t* vram_texture_buffer{nullptr};
};

class TextureLoader {

public:
    static Texture* load_texture(const char* p_path);
};


#endif //RENDERTEST_TEXTURELOADER_H
