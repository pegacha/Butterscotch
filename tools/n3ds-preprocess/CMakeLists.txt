cmake_minimum_required(VERSION 3.21)
project(n3ds_preprocess C)

# Host tool: converts a data.win's textures (tex3ds) and audio (BCWAV) into the 3DS asset layout.

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

set(BS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(n3ds-preprocess
    main.c
    stb_impl.c
    ${BS_ROOT}/src/binary_reader.c
    ${BS_ROOT}/src/data_win.c
    ${BS_ROOT}/src/log.c
    ${BS_ROOT}/src/image/image_decoder.c
    ${BS_ROOT}/vendor/bzip2/bz_internal_error.c
    ${BS_ROOT}/vendor/bzip2/bzlib.c
    ${BS_ROOT}/vendor/bzip2/crctable.c
    ${BS_ROOT}/vendor/bzip2/decompress.c
    ${BS_ROOT}/vendor/bzip2/huffman.c
    ${BS_ROOT}/vendor/bzip2/randtable.c
)

target_include_directories(n3ds-preprocess PRIVATE
    ${BS_ROOT}/src
    ${BS_ROOT}/src/image
    ${BS_ROOT}/vendor
    ${BS_ROOT}/vendor/bzip2
    ${BS_ROOT}/vendor/stb/ds
    ${BS_ROOT}/vendor/stb/image
)

target_compile_definitions(n3ds-preprocess PRIVATE
    ENABLE_WAD14 ENABLE_WAD16 ENABLE_WAD17
    N3DS_PREPROCESS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    N3DS_PREPROCESS_TEXTURE_OVERRIDE_DIR="${BS_ROOT}/resources/n3ds/textureOverrides"
)
set_source_files_properties(${BS_ROOT}/vendor/bzip2/bzlib.c ${BS_ROOT}/vendor/bzip2/decompress.c ${BS_ROOT}/vendor/bzip2/huffman.c
    ${BS_ROOT}/vendor/bzip2/crctable.c ${BS_ROOT}/vendor/bzip2/randtable.c ${BS_ROOT}/vendor/bzip2/bz_internal_error.c
    PROPERTIES COMPILE_OPTIONS "-w")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(n3ds-preprocess PRIVATE m)
endif()
