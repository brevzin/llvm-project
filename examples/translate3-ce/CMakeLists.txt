# CMake project for Compiler Explorer (Tree/IDE mode with CMake enabled).
# All required flags live here so the compiler pane needs none.
cmake_minimum_required(VERSION 3.18)
project(translate3 CXX)

add_executable(demo main.cpp)
target_compile_options(demo PRIVATE
  -std=c++26
  -freflection
  -stdlib=libc++
  -Wno-c23-extensions   # #embed in C++
)
target_link_options(demo PRIVATE -stdlib=libc++)

# Build-time extraction as a POST_BUILD step: pull the i18n_messages section
# out of the linked binary and print the deduplicated .pot into the build
# log. Unlike the program's own extracted_pot(), this demonstrably needs no
# execution of demo.
#
# The records in the section are NUL-terminated and each ends with a
# 'msgstr ""' line; tr turns the NULs into record-separating blank lines and
# awk drops duplicate records (identical literals produce identical records).
# A bracket-quoted variable keeps the awk program free of CMake escaping;
# the newlines are then flattened because the Makefiles generator cannot
# put a multi-line command into a recipe ("missing separator").
set(dedup_pot [=[
  tr -s '\0' '\n' < messages.bin |
    awk '{ rec = rec $0 "\n" }
         /^msgstr ""/ { if (!(rec in seen)) { seen[rec]; printf "%s\n", rec }
                        rec = "" }' > messages.pot
]=])
string(REPLACE "\n" " " dedup_pot "${dedup_pot}")

find_program(OBJCOPY_TOOL NAMES objcopy llvm-objcopy ${CMAKE_OBJCOPY})
if(OBJCOPY_TOOL AND UNIX)
  add_custom_command(TARGET demo POST_BUILD
    COMMAND ${OBJCOPY_TOOL} -O binary --only-section=i18n_messages
            $<TARGET_FILE:demo> messages.bin
    COMMAND sh -c "${dedup_pot}"
    COMMAND ${CMAKE_COMMAND} -E echo
            "--- messages.pot (extracted at build time, objcopy) ---"
    COMMAND ${CMAKE_COMMAND} -E cat messages.pot
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Extracting .pot from demo's i18n_messages section")
endif()
