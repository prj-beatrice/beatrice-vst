cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)

set(major_version 2)
set(minor_version 0)
set(patch_version 0)
set(prerelease_version "-rc.0")
execute_process(
    COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
math(EXPR build_number 0x${GIT_HASH})
message(STATUS "build_number: ${build_number}")

if(DEFINED ENV{CI})
    execute_process(
        COMMAND python -c "import uuid; u=uuid.uuid4().hex; print(*[f'0x{u[8*i:8*i+8]}' for i in range(4)], sep=', ')"
        OUTPUT_VARIABLE processor_uid
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND python -c "import uuid; u=uuid.uuid4().hex; print(*[f'0x{u[8*i:8*i+8]}' for i in range(4)], sep=', ')"
        OUTPUT_VARIABLE controller_uid
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "processor_uid: ${processor_uid}")
    message(STATUS "controller_uid: ${controller_uid}")
else()
    set(processor_uid "0x06659064, 0x6f594d89, 0xb4a2aa65, 0xf13c17b2")
    set(controller_uid "0x3c46dde3, 0xa03045f8, 0x80688f81, 0x88408160")
endif()

project(beatrice VERSION ${major_version}.${minor_version}.${patch_version})
option(BEATRICE_DEV_VERSION "Development version" ON)
if(BEATRICE_DEV_VERSION)
    set(full_version_str ${major_version}.${minor_version}.${patch_version}${prerelease_version}+dev.${GIT_HASH})
else()
    set(full_version_str ${major_version}.${minor_version}.${patch_version}${prerelease_version})
endif()
set(target beatrice_${full_version_str})

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/MT>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/MT>")
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/arch:AVX2>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>")

#find_package(OpenMP REQUIRED)
#if (OpenMP_FOUND)
    #add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/openmp:experimental>")
    #add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/openmp:experimental>")
#endif()

set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES 0)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES 0)
set(SMTG_ENABLE_VSTGUI_SUPPORT 1)

set(vst3sdk_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/vst3sdk)

add_subdirectory(lib/vst3sdk)

smtg_enable_vst3_sdk()

configure_file(src/vst/metadata.h.in src/vst/metadata.h)
set(SMTG_PACKAGE_ICON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/resource/icon.ico)

smtg_add_vst3plugin(${target}
    src/common/parameter_schema.cc
    src/common/parameter_state.cc
    src/common/processor_core_0.cc
    src/common/processor_core_1.cc
    src/common/processor_core_2.cc
    src/common/processor_proxy.cc
    src/common/hungarian_algorithm.cc
    src/vst/controller.cc
    src/vst/editor.cc
    src/vst/factory.cc
    src/vst/parameter.cc
    src/vst/processor.cc
)

add_library(beatricelib STATIC IMPORTED)
set_property(
    TARGET beatricelib
    PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/beatricelib/beatrice.lib
)

target_include_directories(${target}
    PRIVATE src
    PRIVATE lib
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src/vst
)

target_link_libraries(${target}
    PRIVATE beatricelib
    PRIVATE sdk
    PRIVATE vstgui_support
)

if(SMTG_WIN)
    target_sources(${target}
        PRIVATE resource/beatrice.rc
    )
else()
    message(WARNING "Unsupported platform")
endif()

# ----------------------------------------------------------------

set(DISTRIBUTION_DIR ${CMAKE_CURRENT_BINARY_DIR}/beatrice_${full_version_str})
add_custom_target(distribution
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DISTRIBUTION_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory $<TARGET_FILE_DIR:${target}>/../../.. ${DISTRIBUTION_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt ${DISTRIBUTION_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/LICENSES_BUNDLED.txt ${DISTRIBUTION_DIR}
)
add_dependencies(distribution ${target})

if(NOT BEATRICE_DEV_VERSION)
    set(PARAPHERNALIA_DIR ${DISTRIBUTION_DIR}/beatrice_paraphernalia_jvs)
    file(MAKE_DIRECTORY ${PARAPHERNALIA_DIR})
    set(BASE_URL "https://huggingface.co/fierce-cats/beatrice-2.0.0-alpha/resolve/rc.0/rc.0/beatrice_paraphernalia_jvs_156_03000000")
    set(PARAPHERNALIA_FILES
        beatrice_paraphernalia_jvs.toml
        phone_extractor.bin
        pitch_estimator.bin
        speaker_embeddings.bin
        waveform_generator.bin
        embedding_setter.bin
        noimage.png
    )
    foreach(file ${PARAPHERNALIA_FILES})
        file(DOWNLOAD ${BASE_URL}/${file} ${PARAPHERNALIA_DIR}/${file})
    endforeach()
endif()
