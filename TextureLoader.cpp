
#include <cstdio>
#include <png.h>
#include "TextureLoader.h"

Texture* TextureLoader::load_texture(const char* p_path) {
    FILE* file = fopen(p_path, "rb");

    if (!file) {
        printf("ERROR: Failed to open file with path %s!\n", p_path);
        return nullptr;
    }

    png_structp png_ptr;
    png_infop info_ptr;
    png_uint_32 width, height;

    u32 sig_read = 0, row = 0, i = 0, j = 0;
    int bit_depth, color_type, interlace_type;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        printf("ERROR: PNG create read struct failed!\n");
        fclose(file);
        return nullptr;
    }
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        printf("ERROR: PNG info struct init failed!\n");
        fclose(file);
        return nullptr;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        printf("ERROR: PNG reader fatal error!\n");
        fclose(file);
        return nullptr;
    }

    png_init_io(png_ptr, file);
    png_set_sig_bytes(png_ptr, sig_read);
    png_read_info(png_ptr, info_ptr);
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, nullptr, nullptr);
    png_set_strip_16(png_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_expand(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand(png_ptr);

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);

    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

    png_read_update_info(png_ptr, info_ptr);

    png_byte pngType = png_get_color_type(png_ptr, info_ptr);
    if (pngType != PNG_COLOR_TYPE_RGB_ALPHA) {
        printf("ERROR: Only PNG type PNG_COLOR_TYPE_RGB_ALPHA supported atm!\n");
        fclose(file);
        return nullptr;
    }

    size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    png_byte* row_pointers[height];
    for (row = 0; row < height; row++) row_pointers[row] = new png_byte[row_bytes];

    png_read_image(png_ptr, row_pointers);

    u32 x = 0;

    auto *texture_data = new unsigned char[width * height * 4];
    for (i = 0; i < height; i++)
        for (j = 0; j < width; j++) {
            texture_data[x] = row_pointers[i][4 * j];
            texture_data[x + 1] = row_pointers[i][4 * j + 1];
            texture_data[x + 2] = row_pointers[i][4 * j + 2];
            texture_data[x + 3] = ((int)row_pointers[i][4 * j + 3] * 128 / 255);
            x += 4;
        }

    for (row = 0; row < height; row++) delete[] row_pointers[row];

    png_read_end(png_ptr, nullptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

    fclose(file);

    Texture* texture = new Texture {};
    texture->width = width;
    texture->height = height;
    texture->data = texture_data;
    return texture;
}
