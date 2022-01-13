
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
#include <kernel.h>

#include "TextureLoader.h"

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define GS_CENTER 4096.0f
#define SCREEN_CENTER GS_CENTER / 2.0f

// on real PS2. PCSX2 on the other hand seems to like bigger chain sizes
// 500 -> 2370
// 250 -> 2512
// 125 -> 2760
// 62 -> 2818
// 45 -> 2819
// 64 -> 2819
#define MAX_CHAIN_QW_SIZE 64

float delta_time{};
float fps{};

struct FrameDrawState {
    packet2_t* flipPacket{nullptr};
    u8 active_buffer_index{0};
    framebuffer_t frame_buffers[2]{};
    zbuffer_t z_buffer{};
    packet2_t* packets[2]{nullptr, nullptr};
    packet2_t* active_packet{nullptr};
} frame_draw_state{};


u32 time;
u32 last_time;

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
    frame_draw_state.frame_buffers[0] = {
            .address = static_cast<unsigned int>(
                    graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_PSM_32, GRAPH_ALIGN_PAGE)
            ),
            .width = SCREEN_WIDTH,
            .height = SCREEN_HEIGHT,
            .psm = GS_PSM_32,
            .mask = 0,
    };
    frame_draw_state.frame_buffers[1] = {
            .address = static_cast<unsigned int>(
                    graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_PSM_32, GRAPH_ALIGN_PAGE)
            ),
            .width = SCREEN_WIDTH,
            .height = SCREEN_HEIGHT,
            .psm = GS_PSM_32,
            .mask = 0,
    };
    frame_draw_state.z_buffer = {
            .enable = DRAW_ENABLE,
            .method = ZTEST_METHOD_GREATER_EQUAL,
            .address = static_cast<unsigned int>(
                    graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_ZBUF_24, GRAPH_ALIGN_PAGE)
            ),
            .zsm = GS_ZBUF_24,
            .mask = 0,
    };
    graph_initialize(
            static_cast<int>(frame_draw_state.frame_buffers[0].address),
            static_cast<int>(frame_draw_state.frame_buffers[0].width),
            static_cast<int>(frame_draw_state.frame_buffers[0].height),
            static_cast<int>(frame_draw_state.frame_buffers[0].psm),
            0,
            0
    );

    frame_draw_state.flipPacket = packet2_create(4, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    frame_draw_state.packets[0] = packet2_create(MAX_CHAIN_QW_SIZE, P2_TYPE_UNCACHED_ACCL, P2_MODE_CHAIN, 0);
    frame_draw_state.packets[1] = packet2_create(MAX_CHAIN_QW_SIZE, P2_TYPE_UNCACHED_ACCL, P2_MODE_CHAIN, 0);
}

void init_drawing_env(framebuffer_t* p_framebuffer, zbuffer_t* p_z_buffer) {
    packet2_t* packet2 = packet2_create(2, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    packet2_update(packet2, draw_setup_environment(packet2->base, 0, p_framebuffer, p_z_buffer));
    packet2_update(packet2, draw_finish(packet2->next));
    dma_channel_send_packet2(packet2, DMA_CHANNEL_GIF, false);
    dma_wait_fast();
    packet2_free(packet2);
}

void flip_buffers(packet2_t* p_flip_packet, framebuffer_t* p_framebuffer) {
    packet2_update(p_flip_packet, draw_framebuffer(p_flip_packet->base, 0, p_framebuffer));
    packet2_update(p_flip_packet, draw_finish(p_flip_packet->next));
    dma_wait_fast();
    dma_channel_send_packet2(p_flip_packet, DMA_CHANNEL_GIF, false);
    draw_wait_finish();
}

#define SCREEN_CLEAR_X (SCREEN_CENTER - (SCREEN_WIDTH / 2.0f))
#define SCREEN_CLEAR_Y (SCREEN_CENTER - (SCREEN_HEIGHT / 2.0f))
static color_t clear_color{.r = 0x2b, .g = 0x2b, .b = 0x2b, .a = 0x80};

void check_chain_size(packet2_t* packet, int p_qw_to_add) {
    assert(p_qw_to_add < MAX_CHAIN_QW_SIZE);
    if (packet2_get_qw_count(packet) + p_qw_to_add > MAX_CHAIN_QW_SIZE) {
//        printf("SENDING...\n");
        dma_wait_fast();
        dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, false);
        dma_wait_fast();
        frame_draw_state.packets[frame_draw_state.active_buffer_index] = packet2_create(
                MAX_CHAIN_QW_SIZE,
                P2_TYPE_UNCACHED_ACCL,
                P2_MODE_CHAIN, 0
        );
        packet2_free(frame_draw_state.active_packet);
        frame_draw_state.active_packet = frame_draw_state.packets[frame_draw_state.active_buffer_index];
    }
}

void clear_screen(packet2_t* p_packet, zbuffer_t* p_z_buffer) {
    check_chain_size(frame_draw_state.active_packet, 10);
    packet2_chain_open_cnt(p_packet, false, 0, false);
    packet2_update(p_packet, draw_disable_tests(p_packet->next, 0, p_z_buffer));
    packet2_update(
            p_packet,
            draw_clear(
                    p_packet->next,
                    0,
                    SCREEN_CLEAR_X,
                    SCREEN_CLEAR_Y,
                    SCREEN_WIDTH,
                    SCREEN_HEIGHT,
                    clear_color.r,
                    clear_color.g,
                    clear_color.b
            )
    );
    packet2_update(p_packet, draw_enable_tests(p_packet->next, 0, p_z_buffer));
    packet2_chain_close_tag(p_packet);
}

void allocate_and_assign_texture_buffer(Texture* texture) {
    texbuffer_t* vram_texture_buffer = new texbuffer_t{};
    vram_texture_buffer->address = graph_vram_allocate(
            texture->width,
            texture->height,
            texture->format,
            GRAPH_ALIGN_BLOCK
    );
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

void draw_texture(float p_pos_x, float p_pos_y, Texture* texture) {
    texture->texture_rect.v0.x = p_pos_x;
    texture->texture_rect.v0.y = p_pos_y;
    texture->texture_rect.v1.x = p_pos_x + texture->width;
    texture->texture_rect.v1.y = p_pos_y + texture->height;

    check_chain_size(frame_draw_state.active_packet, 5);
    packet2_chain_open_cnt(frame_draw_state.active_packet, false, 0, false);
    draw_enable_blending();
    packet2_update(
            frame_draw_state.active_packet,
            draw_rect_textured(frame_draw_state.active_packet->next, 0, &texture->texture_rect)
    );
    draw_disable_blending();
    packet2_chain_close_tag(frame_draw_state.active_packet);
}

void end_frame() {
    dma_wait_fast();
    dma_channel_send_packet2(frame_draw_state.active_packet, DMA_CHANNEL_GIF, false);

    // disabled for benchmarking
//    graph_wait_vsync();

    frame_draw_state.packets[frame_draw_state.active_buffer_index] = packet2_create(
            MAX_CHAIN_QW_SIZE,
            P2_TYPE_UNCACHED_ACCL,
            P2_MODE_CHAIN, 0
    );
    packet2_free(frame_draw_state.active_packet);

    graph_set_framebuffer_filtered(
            frame_draw_state.frame_buffers[frame_draw_state.active_buffer_index].address,
            frame_draw_state.frame_buffers[frame_draw_state.active_buffer_index].width,
            frame_draw_state.frame_buffers[frame_draw_state.active_buffer_index].psm,
            0,
            0
    );

    frame_draw_state.active_buffer_index ^= 1;

    flip_buffers(frame_draw_state.flipPacket, &frame_draw_state.frame_buffers[frame_draw_state.active_buffer_index]);
}

float randf() {
    uint32_t proto_exp_offset = rand();
    if (proto_exp_offset == 0) {
        return 0;
    }
    return __builtin_ldexpf((float) (rand() | 0x80000001), -32 - __builtin_clz(proto_exp_offset));
}

float random(float p_from, float p_to) {
    return randf() * (p_to - p_from) + p_from;
}

int main() {
    // 5637 PCSX2 on workstation with MAX_CHAIN_QW_SIZE 1024
    // 4208 PCSX2 on workstation with MAX_CHAIN_QW_SIZE 64
    // 2823 on real PS2 -> MAX_CHAIN_QW_SIZE 64 (higher chain size limit gives lower perf)
    printf("Starting render testing\n");
    SifInitRpc(0);
    init_scr();

    int max_texture_count = 10000;
    int current_texture_count = 500;
    float target_fps = 60.0f;
    float texture_positions[max_texture_count][2];
    float texture_directions[max_texture_count][2];

    dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    allocate_buffers();
    init_drawing_env(&frame_draw_state.frame_buffers[0], &frame_draw_state.z_buffer);

    Texture* texture = TextureLoader::load_texture("host:assets/icon.png");
    printf("Loaded texture width: %f\n", texture->width);
    load_texture_into_vram_if_necessary(texture);

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

        frame_draw_state.active_packet = frame_draw_state.packets[frame_draw_state.active_buffer_index];

        clear_screen(frame_draw_state.active_packet, &frame_draw_state.z_buffer);

        packet2_chain_open_cnt(frame_draw_state.active_packet, false, 0, false);
        packet2_utils_gif_add_set(frame_draw_state.active_packet, 1);
        packet2_utils_gs_add_texbuff_clut(
                frame_draw_state.active_packet,
                texture->vram_texture_buffer,
                &texture->clut_buffer
        );
        packet2_chain_close_tag(frame_draw_state.active_packet);

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

            texture_positions[i][0] = x + (texture_directions[i][0] * speed * delta_time);
            texture_positions[i][1] = y + (texture_directions[i][1] * speed * delta_time);

            draw_texture(texture_positions[i][0], texture_positions[i][1], texture);
        }

        end_frame();

        if (fps <= target_fps + 2 && fps >= target_fps - 2) {
            stable_around60_count++;
        } else {
            stable_around60_count = 0;
        }

        if (stable_around60_count >= 30 * target_fps ||
            (fps >= target_fps && current_texture_count >= max_texture_count)) {
            benchmark_running = false;
        }

        if (fps > target_fps) {
            current_texture_count += fps - target_fps;
            if (current_texture_count > max_texture_count) {
                current_texture_count = max_texture_count;
            }
        } else if (fps < target_fps) {
            current_texture_count -= target_fps - fps;
        }
    }

    frame_draw_state.active_packet = frame_draw_state.packets[frame_draw_state.active_buffer_index];
    clear_screen(frame_draw_state.active_packet, &frame_draw_state.z_buffer);
    check_chain_size(frame_draw_state.active_packet, 10);
    packet2_chain_open_end(frame_draw_state.active_packet, false, 0);
    packet2_update(frame_draw_state.active_packet, draw_finish(frame_draw_state.active_packet->next));
    packet2_chain_close_tag(frame_draw_state.active_packet);

    dma_wait_fast();
    dma_channel_send_packet2(frame_draw_state.active_packet, DMA_CHANNEL_GIF, false);
    flip_buffers(frame_draw_state.flipPacket, &frame_draw_state.frame_buffers[frame_draw_state.active_buffer_index]);

    sleep(1);

    scr_clear();
    if (current_texture_count >= max_texture_count) {
        printf("Benchmark aborted.\n\nReason: Reached max texture count.\nTry increasing max_texture_count\n");
        scr_printf("Benchmark aborted.\n\nReason: Reached max texture count.\nTry increasing max_texture_count\n");
    } else {
        printf(
                "Benchmark done.\n\nReached stable %dFPS with %d sprites.\n",
                static_cast<int>(target_fps),
                current_texture_count
        );
        scr_printf(
                "Benchmark done.\n\nReached stable %dFPS with %d sprites.\n",
                static_cast<int>(target_fps),
                current_texture_count
        );
    }

    SleepThread();
}
