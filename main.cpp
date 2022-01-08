
#include <cstdio>
#include <debug.h>
#include <dma.h>
#include <draw_buffers.h>
#include <gs_psm.h>
#include <graph_vram.h>
#include <draw.h>
#include <graph.h>
#include <packet2.h>

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define GS_CENTER 4096.0f
#define SCREEN_CENTER GS_CENTER / 2.0f

framebuffer_t frame_buffers[2];
zbuffer_t z_buffer;

void init_drawing_env();

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

void allocate_buffers() {
    frame_buffers[0] = {
            .address = static_cast<uint32_t>(graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_PSM_32, GRAPH_ALIGN_PAGE)),
            .width = SCREEN_WIDTH,
            .height = SCREEN_HEIGHT,
            .psm = GS_PSM_32,
            .mask = 0,
    };
    frame_buffers[1] = {
            .address = static_cast<uint32_t>(graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_PSM_32, GRAPH_ALIGN_PAGE)),
            .width = SCREEN_WIDTH,
            .height = SCREEN_HEIGHT,
            .psm = GS_PSM_32,
            .mask = 0,
    };
    z_buffer = {
            .enable = DRAW_ENABLE,
            .method = ZTEST_METHOD_GREATER_EQUAL,
            .address = static_cast<uint32_t>(graph_vram_allocate(SCREEN_WIDTH, SCREEN_HEIGHT, GS_ZBUF_24, GRAPH_ALIGN_PAGE)),
            .zsm = GS_ZBUF_24,
            .mask = 0,
    };
    graph_initialize(frame_buffers[0].address, frame_buffers[0].width, frame_buffers[0].height, frame_buffers[0].psm, 0, 0);
}

void init_drawing_env() {
    packet2_t* packet2 = packet2_create(20, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    packet2_update(packet2, draw_setup_environment(packet2->base, 0, frame_buffers, &(z_buffer)));
    packet2_update(packet2, draw_primitive_xyoffset(packet2->next, 0, SCREEN_CENTER - (SCREEN_WIDTH / 2.0F), SCREEN_CENTER - (SCREEN_HEIGHT / 2.0F)));
    packet2_update(packet2, draw_finish(packet2->next));
    dma_channel_send_packet2(packet2, DMA_CHANNEL_GIF, true);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    packet2_free(packet2);
}

int main() {
    printf("Starting render testing\n");

    dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    allocate_buffers();
    init_drawing_env();

    print_stack_trace();

    while (1) {

    }
}
