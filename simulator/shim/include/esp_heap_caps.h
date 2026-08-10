#ifndef SIM_ESP_HEAP_CAPS_H
#define SIM_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM   (1 << 0)
#define MALLOC_CAP_INTERNAL (1 << 1)
#define MALLOC_CAP_DMA      (1 << 2)
#define MALLOC_CAP_8BIT     (1 << 3)
#define MALLOC_CAP_32BIT    (1 << 4)
#define MALLOC_CAP_DEFAULT  (1 << 5)

#ifdef __cplusplus
extern "C" {
#endif

static inline void *heap_caps_malloc(size_t n, unsigned int caps) { (void)caps; return malloc(n); }
static inline void *heap_caps_calloc(size_t cnt, size_t sz, unsigned int caps) { (void)caps; return calloc(cnt, sz); }
static inline void  heap_caps_free(void *p) { free(p); }
static inline size_t heap_caps_get_free_size(unsigned int caps) { (void)caps; return (size_t)-1; }

#ifdef __cplusplus
}
#endif

#endif
