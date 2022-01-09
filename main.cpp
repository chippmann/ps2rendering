
#include <cstdio>
#include <tamtypes.h>
#include <debug.h>
#include <unistd.h>
#include <dma.h>
#include <draw_buffers.h>
#include <gs_psm.h>
#include <graph_vram.h>
#include <draw.h>
#include <graph.h>
#include <packet2.h>
#include <cmath>
#include <packet2_chain.h>
#include <timer.h>
#include <packet2_utils.h>
#include <sifrpc.h>

#include "TextureLoader.h"

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define GS_CENTER 4096.0f
#define SCREEN_CENTER GS_CENTER / 2.0f

float delta_time{};
float fps{};
u32 time;
u32 last_time;

framebuffer_t frame_buffers[2];
zbuffer_t z_buffer;
struct FrameDrawState {
    bool is_frame_empty{true};
    u8 active_buffer_index{0};
    packet2_t* flipPacket{nullptr};
} frame_draw_state{};

void print_stack_trace() {
    unsigned int* stack_trace[256];
    ps2GetStackTrace((unsigned int*) &stack_trace, 256);

    for (int i = 0; i < 256; ++i) {
        if (!stack_trace[i]) {
            break;
        }
        printf("address %d 0x%08x\n", i, (int) stack_trace[i]);
    }
}

void timer_prime() {
    time = *T3_COUNT;

    u32 tmp_delta;
    if (time < last_time) { // counter has wrapped
        tmp_delta = time + (65536 - last_time);
    } else {
        tmp_delta = time - last_time;
    }

    if (tmp_delta == 0) {
        fps = -1;
    } else {
        fps = 15625.0f / (float) tmp_delta; // PAL
    }
    delta_time = tmp_delta / 1000.0f;
    last_time = *T3_COUNT;
//    printf("FPS: %f | Deltatime: %f\n", fps, delta_time);
}

void allocate_buffers() {
    frame_buffers[0] = {
            .address = static_cast<uint32_t>(graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_PSM_32,
                                                                 GRAPH_ALIGN_PAGE)),
            .width = SCREEN_WIDTH,
            .height = SCREEN_HEIGHT,
            .psm = GS_PSM_32,
            .mask = 0,
    };
    frame_buffers[1] = {
            .address = static_cast<uint32_t>(graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_PSM_32,
                                                                 GRAPH_ALIGN_PAGE)),
            .width = SCREEN_WIDTH,
            .height = SCREEN_HEIGHT,
            .psm = GS_PSM_32,
            .mask = 0,
    };
    z_buffer = {
            .enable = DRAW_ENABLE,
            .method = ZTEST_METHOD_GREATER_EQUAL,
            .address = static_cast<uint32_t>(graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_ZBUF_24,
                                                                 GRAPH_ALIGN_PAGE)),
            .zsm = GS_ZBUF_24,
            .mask = 0,
    };
    graph_initialize(frame_buffers[0].address, frame_buffers[0].width, frame_buffers[0].height, frame_buffers[0].psm, 0,
                     0);

    frame_draw_state.flipPacket = packet2_create(4, P2_TYPE_UNCACHED_ACCL, P2_MODE_NORMAL, 0);
}

void init_drawing_env() {
    packet2_t* packet2 = packet2_create(20, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    packet2_update(packet2, draw_setup_environment(packet2->base, 0, frame_buffers, &(z_buffer)));
    packet2_update(packet2, draw_primitive_xyoffset(packet2->next, 0, SCREEN_CENTER - (SCREEN_WIDTH / 2.0F),
                                                    SCREEN_CENTER - (SCREEN_HEIGHT / 2.0F)));
    packet2_update(packet2, draw_finish(packet2->next));
    dma_channel_send_packet2(packet2, DMA_CHANNEL_GIF, true);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    packet2_free(packet2);
}

void clear_screen(color_t p_clear_color) {
    packet2_t* packet2 = packet2_create(36, P2_TYPE_NORMAL, P2_MODE_CHAIN, false);
    packet2_chain_open_end(packet2, 0, 0);
    packet2_update(packet2, draw_disable_tests(packet2->next, 0, &z_buffer));
    packet2_update(packet2, draw_clear(packet2->next, 0,
                                       2048.0F - (SCREEN_WIDTH / 2.0f), 2048.0F - (SCREEN_HEIGHT / 2.0f),
                                       SCREEN_WIDTH, SCREEN_HEIGHT,
                                       p_clear_color.r, p_clear_color.g, p_clear_color.b));
    packet2_update(packet2, draw_enable_tests(packet2->next, 0, &z_buffer));
    packet2_update(packet2, draw_finish(packet2->next));
    packet2_chain_close_tag(packet2);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    dma_channel_send_packet2(packet2, DMA_CHANNEL_GIF, true);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    packet2_free(packet2);
}

void begin_frame_if_needed() {
    if (frame_draw_state.is_frame_empty) {
        frame_draw_state.is_frame_empty = false;
        clear_screen({.r = 0x2b, .g = 0x2b, .b = 0x2b, .a = 0x80});
    }
}

void allocate_and_assign_texture_buffer(Texture* texture) {
    texbuffer_t* vram_texture_buffer = new texbuffer_t{};
    vram_texture_buffer->address = graph_vram_allocate(texture->width, texture->height, texture->format,
                                                       GRAPH_ALIGN_BLOCK);
    vram_texture_buffer->width = texture->width;
    vram_texture_buffer->psm = texture->format;
    vram_texture_buffer->info = {
            .width = draw_log2(texture->width),
            .height = draw_log2(texture->height),
            .components = TEXTURE_COMPONENTS_RGBA,
            .function = TEXTURE_FUNCTION_MODULATE
    };
    texture->vram_texture_buffer = vram_texture_buffer;
}

void load_texture_into_vram_if_necessary(Texture* texture) {
    if (!texture->vram_texture_buffer) {
        printf("Texture not in vram. Uploading\n");
        allocate_and_assign_texture_buffer(texture);

        packet2_t* packet2 = packet2_create(15, P2_TYPE_NORMAL, P2_MODE_CHAIN, false);
        packet2_update(
                packet2,
                draw_texture_transfer(
                        packet2->base,
                        (void*) texture->data,
                        texture->width,
                        texture->height,
                        texture->format,
                        texture->vram_texture_buffer->address,
                        texture->vram_texture_buffer->width
                )
        );
        packet2_chain_open_cnt(packet2, 0, 0, 0);
        packet2_update(packet2, draw_texture_wrapping(packet2->next, 0, &texture->wrap_settings));
        packet2_chain_close_tag(packet2);
        packet2_update(packet2, draw_texture_flush(packet2->next));
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        dma_channel_send_packet2(packet2, DMA_CHANNEL_GIF, true);
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        packet2_free(packet2);
    }
}

void draw_texture(packet2_t* chain_packet, float p_pos_x, float p_pos_y, Texture* texture) {
//    float texture_rect_s;
//    float texture_rect_t;
//    float max;
//    if (texture->width > texture->height) {
//        max = texture->width;
//    } else {
//        max = texture->height;
//    }
//    float texture_rect_max = texture_rect_t = texture_rect_s = max;//fmax(texture->width, texture->height);
//
//    if (texture->width > texture->height) {
//        texture_rect_t = texture_rect_max / (static_cast<float>(texture->width) / static_cast<float>(texture->height));
//    } else if (texture->height > texture->width) {
//        texture_rect_s = texture_rect_max / (static_cast<float>(texture->height) / static_cast<float>(texture->width));
//    }

    texture->texture_rect.v0.x = p_pos_x;
    texture->texture_rect.v0.y = p_pos_y;
    texture->texture_rect.v1.x = p_pos_x + texture->width;
    texture->texture_rect.v1.y = p_pos_y + texture->height;

//    texrect_t texture_rect{
//            .v0 = {
//                    .x = p_pos_x,
//                    .y = p_pos_y,
//                    .z = 0
//            },
//            .t0  = {
//                    .s = 0,
//                    .t = 0
//            },
//            .v1 = {
//                    .x = p_pos_x + texture->width,
//                    .y = p_pos_y + texture->height,
//                    .z = 0
//            },
//            .t1  = {
//                    .s = texture->width,
//                    .t = texture->height
//            },
//            .color = {
//                    .r = 0x80,
//                    .g = 0x80,
//                    .b = 0x80,
//                    .a = 0x80,
//                    .q = 1.0f
//            }
//    };

//    packet2_t* chain_packet = packet2_create(12, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    packet2_utils_gif_add_set(chain_packet, 1);
    packet2_utils_gs_add_texbuff_clut(chain_packet, texture->vram_texture_buffer, &texture->clut_buffer);
    draw_enable_blending();
    packet2_update(chain_packet, draw_rect_textured(chain_packet->next, 0, &texture->texture_rect));
//    packet2_update(chain_packet, draw_primitive_xyoffset(chain_packet->next, 0, SCREEN_CENTER - (SCREEN_WIDTH / 2.0F),
//                                                    SCREEN_CENTER - (SCREEN_HEIGHT / 2.0F)));
    draw_disable_blending();
//    packet2_update(chain_packet, draw_finish(chain_packet->next));
//    dma_channel_wait(DMA_CHANNEL_GIF, 0);
//    dma_channel_send_packet2(chain_packet, DMA_CHANNEL_GIF, true);
//    packet2_free(chain_packet);
}

void end_frame() {
//    graph_wait_vsync();
    if (!frame_draw_state.is_frame_empty) {
        graph_set_framebuffer_filtered(frame_buffers[frame_draw_state.active_buffer_index].address,
                                       frame_buffers[frame_draw_state.active_buffer_index].width,
                                       frame_buffers[frame_draw_state.active_buffer_index].psm, 0, 0);
        frame_draw_state.active_buffer_index ^= 1;
        frame_draw_state.is_frame_empty = true;
        packet2_update(frame_draw_state.flipPacket, draw_framebuffer(frame_draw_state.flipPacket->base, 0,
                                                                     &frame_buffers[frame_draw_state.active_buffer_index]));
        packet2_update(frame_draw_state.flipPacket, draw_finish(frame_draw_state.flipPacket->next));
        dma_channel_send_packet2(frame_draw_state.flipPacket, DMA_CHANNEL_GIF, true);
        draw_wait_finish();
    }
}

float randf() {
    uint32_t proto_exp_offset = rand();
    if (proto_exp_offset == 0) {
        return 0;
    }
    return __builtin_ldexpf((float)(rand() | 0x80000001), -32 - __builtin_clz(proto_exp_offset));
}

float random(float p_from, float p_to) {
    return randf() * (p_to - p_from) + p_from;
}

packet2_t* start_chain(int texture_count) {
    // 1 texture = 8 qw
    // 3 = pre and after
    packet2_t* packet2 = packet2_create((texture_count * 8) + 3, P2_TYPE_NORMAL, P2_MODE_CHAIN, false);
//    packet2_chain_open_end(packet2, 0, 0);
    packet2_chain_open_cnt(packet2, false, 0, false);
    return packet2;
}

int main() {
    // 5349 PCSX2 on framework laptop
    // 1944 on real PS2
    printf("Starting render testing\n");
    SifInitRpc(0);
    init_scr();

    int max_texture_count = 10000;
    int current_texture_count = 500;
    float texture_positions[max_texture_count][2];
    float texture_directions[max_texture_count][2];

    dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    allocate_buffers();
    init_drawing_env();

    Texture* texture = TextureLoader::load_texture("host:assets/icon.png");
    printf("Loaded texture width: %d\n", texture->width);

    int positional_screen_size_x = SCREEN_WIDTH - texture->width;
    int positional_screen_size_y = SCREEN_HEIGHT - texture->height;
    float speed = 10;
    for (int i = 0; i < max_texture_count; ++i) {
        texture_positions[i][0] = rand() % positional_screen_size_x;
        texture_positions[i][1] = rand() % positional_screen_size_y;
        texture_directions[i][0] = random(-1.0f, 1.0f);
        texture_directions[i][1] = random(-1.0f, 1.0f);
    }

    bool benchmark_running = true;
    int stable_around60_count = 0;
    while (benchmark_running) {
        timer_prime();
        begin_frame_if_needed();
        load_texture_into_vram_if_necessary(texture);
        packet2_t* chain_packet = start_chain(current_texture_count);
        packet2_update(chain_packet, draw_primitive_xyoffset(chain_packet->next, 0, SCREEN_CENTER, SCREEN_CENTER));

        for (int i = 0; i < current_texture_count; ++i) {
            float x = texture_positions[i][0];
            float y = texture_positions[i][1];

            if (x <= 0) {
                texture_directions[i][0] = random(0.0f, 1.0f);
            } else if (x >= positional_screen_size_x) {
                texture_directions[i][0] = random(-1.0f, 0.0f);
            }
            if (y <= 0) {
                texture_directions[i][1] = random(0.0f, 1.0f);
            } else if (y >= positional_screen_size_y) {
                texture_directions[i][1] = random(-1.0f, 0.0f);
            }

//            if (i == 0) {
//                printf("pos_x: %d pos_y %d dir_x: %f dir_y: %f\n", x, y, texture_directions[i][0], texture_directions[i][1]);
//                printf("target_x: %f\n", x, y, texture_directions[i][0], texture_directions[i][1]);
//            }

            texture_positions[i][0] = x + (texture_directions[i][0] * speed * delta_time);
            texture_positions[i][1] = y + (texture_directions[i][1] * speed * delta_time);

            draw_texture(chain_packet, texture_positions[i][0], texture_positions[i][1], texture);
        }

//        packet2_update(chain_packet, draw_finish(chain_packet->next));
        packet2_chain_close_tag(chain_packet);
//        packet2_print_qw_count(chain_packet);
//        while (true){}
//        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        dma_wait_fast();
        dma_channel_send_packet2(chain_packet, DMA_CHANNEL_GIF, true);
        dma_wait_fast();
//        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        packet2_free(chain_packet);

        end_frame();

        if (fps <= 65 && fps >= 55) {
            stable_around60_count++;
        } else {
            stable_around60_count = 0;
        }

        if (stable_around60_count >= 10*60 || (fps >= 60 && current_texture_count >= max_texture_count)) {
            benchmark_running = false;
        }

        if (fps > 60) {
            current_texture_count += fps - 60;
            if (current_texture_count > max_texture_count) {
                current_texture_count = max_texture_count;
            }
        } else if (fps < 60) {
            current_texture_count -= 60 - fps;
        }
    }

    begin_frame_if_needed();
    clear_screen({.r = 0x0, .g = 0x0, .b = 0x0, .a = 0x80});
    end_frame();

    scr_clear();
    if (current_texture_count >= max_texture_count) {
        printf("Benchmark aborted.\n\nReason: Reached max texture count.\nTry increasing max_texture_count\n");
        scr_printf("Benchmark aborted.\n\nReason: Reached max texture count.\nTry increasing max_texture_count\n");
    } else {
        printf("Benchmark done.\n\nReached stable 60FPS with %d sprites.\n", current_texture_count);
        scr_printf("Benchmark done.\n\nReached stable 60FPS with %d sprites.\n", current_texture_count);
    }

    while (1){

    }
}
