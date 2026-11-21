# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FILESYSTEM_VERSION LIMIT_COUNT 1)
project(FilesystemSFS VERSION ${FILESYSTEM_VERSION} LANGUAGES C)
include(CTest)
include(FetchContent)
find_package(OpenSSL REQUIRED)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(infiltratr_common
  GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git
  GIT_TAG d23a52693fd3212368d327e1c1302e07ae9510ec)
FetchContent_MakeAvailable(infiltratr_common)
set_property(DIRECTORY "${infiltratr_common_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
if(NOT CMAKE_OBJCOPY)
  find_program(CMAKE_OBJCOPY NAMES objcopy llvm-objcopy REQUIRED)
endif()
set(SFS_FIRMWARE_DIR "${CMAKE_CURRENT_BINARY_DIR}/firmware")
set(SFS_FIRMWARE_ELF "${SFS_FIRMWARE_DIR}/SFS.so")
add_custom_command(OUTPUT "${SFS_FIRMWARE_ELF}"
  COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_SOURCE_DIR}/module/firmware"
    -B "${SFS_FIRMWARE_DIR}" -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCOMMON_INCLUDE_DIR=${infiltratr_common_SOURCE_DIR}/include"
  COMMAND "${CMAKE_COMMAND}" --build "${SFS_FIRMWARE_DIR}" --parallel 2
  DEPENDS module/firmware/CMakeLists.txt module/firmware/runtime.c
    module/sfs_module.c module/sfs_reader.c module/sfs_reader.h
    native/filesystems/sfs/core/sfs_core.c
    native/filesystems/sfs/core/sfs_core.h
  VERBATIM)
add_custom_target(sfs-firmware-elf DEPENDS "${SFS_FIRMWARE_ELF}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h"
  "#define LD_VERSION \"${PROJECT_VERSION}\"\n")

# Actual Common V2 SFS0 root/bitmap decoder and canonical core only.
# The host-service V1 worker is retained but not part of the installed ELF.
add_library(sfs-driver SHARED
  module/sfs_module.c module/sfs_reader.c
  native/filesystems/sfs/core/sfs_core.c)
set_target_properties(sfs-driver PROPERTIES
  PREFIX "" OUTPUT_NAME "SFS" SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_include_directories(sfs-driver PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/sfs/core"
  "${infiltratr_common_SOURCE_DIR}/include")
target_compile_options(sfs-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(sfs-driver PRIVATE
  -Wl,-z,defs
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/module/v2-only.map")
target_link_libraries(sfs-driver PRIVATE InfiltratrCommon::Common)
add_dependencies(sfs-driver sfs-firmware-elf)
set_property(TARGET sfs-driver APPEND PROPERTY LINK_DEPENDS "${SFS_FIRMWARE_ELF}")
add_custom_command(TARGET sfs-driver POST_BUILD
  COMMAND "${CMAKE_OBJCOPY}" --add-section ".infiltrator_boot=${SFS_FIRMWARE_ELF}"
    --set-section-flags .infiltrator_boot=readonly "$<TARGET_FILE:sfs-driver>"
  VERBATIM)

if(BUILD_TESTING)
  add_library(sfs-historical-test-support SHARED
    native/sfs_native.c
    native/sfs_worker.c
    module/sfs_module.c module/sfs_reader.c
    compat/legacy_services.c
    native/filesystems/sfs/core/sfs_core.c
    fixtures/sfs_codec.c
    compat/fixture_bridge.c)
  set_source_files_properties(native/sfs_worker.c PROPERTIES
    COMPILE_DEFINITIONS "main=ifs_sfs_worker_main")
  target_include_directories(sfs-historical-test-support PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/sfs/core"
    "${infiltratr_common_SOURCE_DIR}/include")
  target_compile_definitions(sfs-historical-test-support PRIVATE
    _GNU_SOURCE _FILE_OFFSET_BITS=64)
  target_compile_options(sfs-historical-test-support PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(sfs-historical-test-support PRIVATE
    InfiltratrCommon::Common OpenSSL::Crypto)

  add_executable(sfs-module-test
    tests/test_sfs_module.c
    native/filesystems/sfs/core/sfs_core.c)
  target_include_directories(sfs-module-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/sfs/core"
    "${infiltratr_common_SOURCE_DIR}/include")
  target_compile_options(sfs-module-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(sfs-module-test PRIVATE
    sfs-driver InfiltratrCommon::Common ${CMAKE_DL_LIBS})
  add_test(NAME sfs-v2-multi-volume-probe-and-mount COMMAND sfs-module-test)
  add_test(NAME sfs-freestanding-real-namespace
    COMMAND sfs-module-test "${SFS_FIRMWARE_ELF}")
  add_test(NAME sfs-freestanding-v2-elf
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_firmware_v2.sh"
      "$<TARGET_FILE:sfs-driver>" "${SFS_FIRMWARE_ELF}")

  add_executable(sfs-canonical-core-link-test
    tests/test_canonical_core.c
    native/filesystems/sfs/core/sfs_core.c)
  target_include_directories(sfs-canonical-core-link-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/sfs/core")
  target_compile_options(sfs-canonical-core-link-test PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME sfs-canonical-core-link COMMAND sfs-canonical-core-link-test)
  add_test(NAME sfs-production-v2-only
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_v2_exports.sh"
      "$<TARGET_FILE:sfs-driver>")
endif()

install(TARGETS sfs-driver LIBRARY DESTINATION /System/Filesystems)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/SFS.pdf"
  DESTINATION /System/Resources/Documentation RENAME Filesystem-SFS.pdf)
