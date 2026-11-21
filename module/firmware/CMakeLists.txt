# SPDX-License-Identifier: GPL-3.0-or-later
# Freestanding Common V2 SFS reader from same hosted format source.
cmake_minimum_required(VERSION 3.20)
project(SfsBootReader LANGUAGES C)
set(COMMON_INCLUDE_DIR "" CACHE PATH "Pinned Common V2 SDK includes")
if(NOT EXISTS "${COMMON_INCLUDE_DIR}/infiltratr/filesystem_module_v2.h")
    message(FATAL_ERROR "COMMON_INCLUDE_DIR must be the pinned Common V2 SDK")
endif()
add_library(sfs-boot MODULE
    ../sfs_module.c
    ../sfs_reader.c
    ../../native/filesystems/sfs/core/sfs_core.c
    runtime.c)
set_target_properties(sfs-boot PROPERTIES PREFIX "" OUTPUT_NAME "SFS"
    SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_compile_features(sfs-boot PRIVATE c_std_11)
target_compile_definitions(sfs-boot PRIVATE IFS_SFS_FIRMWARE_BUILD=1)
target_include_directories(sfs-boot PRIVATE "${COMMON_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../native/filesystems/sfs/core")
target_compile_options(sfs-boot PRIVATE
    -Wall -Wextra -Wpedantic -Werror -ffreestanding -fno-builtin
    -fno-tree-loop-distribute-patterns -fno-stack-protector -mno-red-zone
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
target_link_options(sfs-boot PRIVATE
    -nostdlib -Wl,-Bsymbolic,-z,defs,-z,noexecstack,-z,relro,-z,now)
