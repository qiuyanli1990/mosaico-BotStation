#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t size, uint32_t caps);
