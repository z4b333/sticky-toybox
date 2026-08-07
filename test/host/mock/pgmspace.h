#pragma once
#include <cstdint>
#define PROGMEM
inline uint8_t pgm_read_byte(const void* p) { return *(const uint8_t*)p; }
inline uint16_t pgm_read_word(const void* p) { return *(const uint16_t*)p; }
inline uint32_t pgm_read_dword(const void* p) { return *(const uint32_t*)p; }
