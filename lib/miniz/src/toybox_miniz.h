/* Toybox only needs miniz's low-level streaming inflate (tinfl), for reading
 * EPUB zip entries. The archive, deflate, stdio, and zlib-compatibility
 * layers are compiled out. Include this header instead of miniz.h so every
 * translation unit sees the same configuration.
 *
 * Layout learned from CrossPoint's vendoring of the same library, including
 * the load-bearing part below. */
#pragma once

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES

/* The ESP32 mask ROM exports tinfl_* at fixed addresses via DIRECT linker
 * script assignments, which override object-file definitions -- without
 * these renames the firmware silently binds to the ROM's build (built with
 * TINFL_LESS_MEMORY, a different tinfl_decompressor layout) and corrupts
 * inflate state on real data. Rename so the linker can never capture them.
 * CrossPoint hit this first and documented it; the prefix here is toybox_
 * so a guest build linked next to CrossPoint's copy cannot collide. */
#define tinfl_decompress toybox_tinfl_decompress
#define tinfl_decompress_mem_to_heap toybox_tinfl_decompress_mem_to_heap
#define tinfl_decompress_mem_to_mem toybox_tinfl_decompress_mem_to_mem
#define tinfl_decompress_mem_to_callback toybox_tinfl_decompress_mem_to_callback
#define mz_crc32 toybox_mz_crc32
#define mz_adler32 toybox_mz_adler32
#define mz_free toybox_mz_free

/* Include the vendored miniz by relative path: ESP-IDF ships a ROM miniz.h
 * with the SAME include guard but a different (TINFL_LESS_MEMORY) struct
 * layout -- resolving miniz.h through the platform include path would
 * silently compile against the wrong structures. */
#include "../third_party/miniz.h"
