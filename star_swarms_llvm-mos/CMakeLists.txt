cmake_minimum_required(VERSION 3.18)

add_subdirectory(tools)

set(LLVM_MOS_PLATFORM rp6502)

find_package(llvm-mos-sdk REQUIRED)

project(MY-RP6502-PROJECT)

add_executable(star_swarms)
rp6502_executable(star_swarms)
target_include_directories(star_swarms PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)
target_sources(star_swarms PRIVATE
    src/bitmap_graphics.c
    src/star_swarms.c
)
