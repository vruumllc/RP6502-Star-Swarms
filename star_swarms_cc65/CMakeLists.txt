cmake_minimum_required(VERSION 3.13)

project(MY-RP6502-PROJECT C ASM)

add_subdirectory(tools)

add_executable(star_swarms)
rp6502_asset(star_swarms 0x1B000 images/space0.bin)
rp6502_asset(star_swarms 0x1B080 images/space1.bin)
rp6502_asset(star_swarms 0x1B100 images/space2.bin)
rp6502_asset(star_swarms 0x1B180 images/space3.bin)
rp6502_asset(star_swarms 0x1B200 images/space4.bin)
rp6502_asset(star_swarms 0x1B280 images/space5.bin)
rp6502_asset(star_swarms 0x1B300 images/space6.bin)
rp6502_asset(star_swarms 0x1B380 images/space7.bin)
rp6502_asset(star_swarms 0x1B400 images/space8.bin)
rp6502_asset(star_swarms 0x1B480 images/space9.bin)
rp6502_asset(star_swarms 0x1B500 images/space10.bin)
rp6502_asset(star_swarms 0x1B580 images/space11.bin)
rp6502_asset(star_swarms 0x1B600 images/space12.bin)
rp6502_asset(star_swarms 0x1B680 images/space13.bin)
rp6502_asset(star_swarms 0x1B700 images/space14.bin)
rp6502_asset(star_swarms 0x1B780 images/space15.bin)
rp6502_asset(star_swarms 0x1B800 images/spaceship.bin)
rp6502_asset(star_swarms 0x1BA00 images/alien_blue.bin)
rp6502_asset(star_swarms 0x1BC00 images/alien_green.bin)
rp6502_asset(star_swarms 0x1BE00 images/alien_yellow.bin)
rp6502_asset(star_swarms 0x1C000 images/alien_pink.bin)
rp6502_asset(star_swarms 0x1C200 images/alien_red.bin)
rp6502_asset(star_swarms 0x1C400 images/alien_white.bin)
rp6502_asset(star_swarms 0x1C600 images/alien_missle.bin)
rp6502_asset(star_swarms 0x1C620 images/missle.bin)
rp6502_asset(star_swarms 0x1C640 images/explosion.bin)
rp6502_executable(star_swarms
    space0.bin.rp6502
    space1.bin.rp6502
    space2.bin.rp6502
    space3.bin.rp6502
    space4.bin.rp6502
    space5.bin.rp6502
    space6.bin.rp6502
    space7.bin.rp6502
    space8.bin.rp6502
    space9.bin.rp6502
    space10.bin.rp6502
    space11.bin.rp6502
    space12.bin.rp6502
    space13.bin.rp6502
    space14.bin.rp6502
    space15.bin.rp6502
    spaceship.bin.rp6502
    alien_blue.bin.rp6502
    alien_green.bin.rp6502
    alien_yellow.bin.rp6502
    alien_pink.bin.rp6502
    alien_red.bin.rp6502
    alien_white.bin.rp6502
    alien_missle.bin.rp6502
    missle.bin.rp6502
    explosion.bin.rp6502
)
target_include_directories(star_swarms PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)
target_sources(star_swarms PRIVATE
    src/bitmap_graphics.c
    src/star_swarms.c
)
