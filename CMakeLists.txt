cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_TOOLCHAIN_FILE /home/cedric/projects/01_private/untitled8/toolchain/PS2Toolchain.cmake)

project(rendertest)



add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} -ldebug -lkernel -ldraw -lcdvd -lgraph -lmath3d -lpacket -ldma -lpacket2 -lpad -laudsrv -lc -lstdc++ -lpng -lz -lfreetype)