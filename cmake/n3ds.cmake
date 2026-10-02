# Nintendo 3DS (devkitARM + libctru + citro2d/citro3d). Included from CMakeLists.txt when PLATFORM=n3ds.
# Output: butterscotch.3dsx (romfs from N3DS_ROMFS_DIR if it exists).

option(N3DS_ENABLE_LTO "Enable link-time optimization for the 3DS target" ON)
option(N3DS_ENABLE_AUDIO "Build the NDSP audio system (BCWAV from n3ds-preprocess)" OFF)
set(N3DS_APP_NAME "Butterscotch" CACHE STRING "3DS title name (SMDH)")
set(N3DS_APP_DESCRIPTION "GameMaker runner" CACHE STRING "3DS title description (SMDH)")
set(N3DS_APP_AUTHOR "Butterscotch" CACHE STRING "3DS title author (SMDH)")
set(N3DS_ROMFS_DIR "${CMAKE_SOURCE_DIR}/resources/n3ds/romfs" CACHE PATH "Directory packed as the .3dsx romfs")

add_compile_definitions(PLATFORM_N3DS)

if(NOT N3DS_ENABLE_AUDIO)
    get_target_property(_n3ds_sources butterscotch SOURCES)
    list(REMOVE_ITEM _n3ds_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/n3ds/n3ds_audio_system.c")
    set_target_properties(butterscotch PROPERTIES SOURCES "${_n3ds_sources}")
else()
    target_compile_definitions(butterscotch PRIVATE N3DS_ENABLE_AUDIO)
endif()

foreach(_opt ENABLE_VM_GML_PROFILER ENABLE_VM_TRACING ENABLE_VM_OPCODE_PROFILER ENABLE_VM_STUB_LOGS)
    option(${_opt} "" OFF)
    if(${_opt})
        add_compile_definitions(${_opt})
    endif()
endforeach()

target_include_directories(butterscotch PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor
    ${CMAKE_SOURCE_DIR}/vendor/stb/ds
    ${CMAKE_SOURCE_DIR}/vendor/stb/image
)

target_compile_options(butterscotch PRIVATE -ffunction-sections -fdata-sections)
target_link_options(butterscotch PRIVATE -Wl,--gc-sections)
if(N3DS_ENABLE_LTO)
    target_compile_options(butterscotch PRIVATE -flto)
    target_link_options(butterscotch PRIVATE -flto)
endif()

target_link_libraries(butterscotch PRIVATE bzip2 stb_ds sha1 stb_vorbis citro2d citro3d ctru m)

set(N3DS_SMDH_FILE "${CMAKE_CURRENT_BINARY_DIR}/butterscotch.smdh")
ctr_generate_smdh(OUTPUT "${N3DS_SMDH_FILE}"
    NAME "${N3DS_APP_NAME}"
    DESCRIPTION "${N3DS_APP_DESCRIPTION}"
    AUTHOR "${N3DS_APP_AUTHOR}"
)
if(EXISTS "${N3DS_ROMFS_DIR}")
    ctr_create_3dsx(butterscotch_3dsx TARGET butterscotch OUTPUT butterscotch.3dsx SMDH "${N3DS_SMDH_FILE}" ROMFS "${N3DS_ROMFS_DIR}")
else()
    ctr_create_3dsx(butterscotch_3dsx TARGET butterscotch OUTPUT butterscotch.3dsx SMDH "${N3DS_SMDH_FILE}")
endif()

option(N3DS_DIAG_PATTERN "Draw renderer test squares (debug)" OFF)
if(N3DS_DIAG_PATTERN)
    target_compile_definitions(butterscotch PRIVATE N3DS_DIAG_PATTERN)
endif()
