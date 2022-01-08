
#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H

int main() {
    printf("HUHU\n");

    printf("Before freetype init\n");
    FT_Library library;
    int error = FT_Init_FreeType(&library);
    if (error) {
        printf("Error during freetype init\n");
    } else {
        printf("freetype init successful\n");
    }
    printf("After freetype init\n");

    while (1) {

    }
}
