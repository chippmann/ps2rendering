cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/toolchain/PS2Toolchain.cmake)

project(rendertest)



add_executable(${PROJECT_NAME} main.cpp TextureLoader.cpp TextureLoader.h)
target_link_libraries(${PROJECT_NAME} -ldebug -lkernel -ldraw -lcdvd -lgraph -lmath3d -lpacket -ldma -lpacket2 -lpad -laudsrv -lc -lstdc++ -lpng -lz -lfreetype)

add_custom_command(
        TARGET ${PROJECT_NAME}
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND}
        -DSRC_FOLDER=${CMAKE_CURRENT_SOURCE_DIR}/assets
        -DTARGET_FOLDER=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/copy_assets.cmake
        COMMENT "Copying assets..."
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)