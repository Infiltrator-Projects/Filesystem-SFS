cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FILESYSTEM_VERSION LIMIT_COUNT 1)
project(FilesystemSFS VERSION ${FILESYSTEM_VERSION} LANGUAGES C)
include(CTest)
find_package(OpenSSL REQUIRED)
include(FetchContent)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(infiltratr_common GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git GIT_TAG 4c92c8dcd7e03afd4bab3105e143dbabd72c8adc)
FetchContent_MakeAvailable(infiltratr_common)
# Common is a link-time dependency; its development files must not be installed
# by this standalone filesystem package.
set_property(DIRECTORY "${infiltratr_common_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h" "#define LD_VERSION \"${PROJECT_VERSION}\"\n")
# The Linux sfs.ko adapter includes this exact portable format core.
# Link it into SFS.so instead of leaving two independent policy engines.
add_library(sfs-driver SHARED native/sfs_native.c native/sfs_worker.c module/sfs_module.c compat/legacy_services.c
 native/filesystems/sfs/core/sfs_core.c fixtures/sfs_codec.c compat/fixture_bridge.c)
set_source_files_properties(native/sfs_worker.c PROPERTIES COMPILE_DEFINITIONS "main=ifs_sfs_worker_main")
set_target_properties(sfs-driver PROPERTIES PREFIX "" OUTPUT_NAME "SFS" SUFFIX ".so")
target_include_directories(sfs-driver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native" "${CMAKE_CURRENT_SOURCE_DIR}/compat" "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(sfs-driver PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
target_compile_options(sfs-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(sfs-driver PRIVATE InfiltratrCommon::Common OpenSSL::Crypto)
if(BUILD_TESTING)
 add_executable(sfs-module-test tests/test_sfs_module.c)
 target_include_directories(sfs-module-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
 target_link_libraries(sfs-module-test PRIVATE sfs-driver InfiltratrCommon::Common)
 add_test(NAME sfs-module-interface COMMAND sfs-module-test)
 add_executable(sfs-canonical-core-link-test tests/test_canonical_core.c)
 target_include_directories(sfs-canonical-core-link-test PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/sfs/core")
 target_compile_options(sfs-canonical-core-link-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
 target_link_libraries(sfs-canonical-core-link-test PRIVATE sfs-driver)
 add_test(NAME sfs-canonical-core-link COMMAND sfs-canonical-core-link-test)
endif()
install(TARGETS sfs-driver LIBRARY DESTINATION /System/Filesystems)
# Install only this filesystem's illustrated PDF beside its native shared object.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/SFS.pdf"
        DESTINATION /System/Resources/Documentation RENAME Filesystem-SFS.pdf)
