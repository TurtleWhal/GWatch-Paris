/**
 * @file lv_mem_psram.c
 *
 * LVGL custom allocator (LV_USE_CUSTOM_MALLOC) that routes every lv_malloc()
 * into PSRAM via ESP heap_caps instead of the internal-SRAM heap.
 *
 * Why: LVGL's default CLIB malloc pulled all widget/style/layout allocations
 * from internal SRAM — they're small (< 1 KB) and SPIRAM_MALLOC_ALWAYSINTERNAL
 * forces sub-1 KB mallocs internal — so the built screen tree plus a full
 * notification list drove free internal SRAM down to ~3 KB (measured), which
 * is where boot-time BLE mallocs and large notification renders start failing.
 * PSRAM has megabytes free. This mirrors the PsramAllocator pattern already
 * used for notification icon buffers in ble.hpp.
 *
 * The ping-pong DMA draw buffers do NOT go through here — esp_lvgl_port
 * allocates those itself with buff_dma=true (see display.cpp), so they stay in
 * internal DMA-capable SRAM where the LCD SPI master needs them. Only the
 * DMA-agnostic object metadata moves to PSRAM.
 *
 * Modelled on LVGL's own lv_mem_core_clib.c, with malloc/realloc/free swapped
 * for the heap_caps SPIRAM equivalents. Compiled only when CUSTOM malloc is the
 * selected backend, so flipping the Kconfig choice back cleanly drops this out.
 */

#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include "esp_heap_caps.h"

/* PSRAM, byte-addressable. LVGL objects are never DMA targets, so no
 * MALLOC_CAP_DMA / internal requirement here. */
#define LV_PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
    return; /* PSRAM heap is registered by the startup code before app_main. */
}

void lv_mem_deinit(void)
{
    return; /* Nothing to tear down — we don't own a private pool. */
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    /* Not supported: allocations come from the shared PSRAM heap, not a
     * fixed pool we manage. */
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
    return;
}

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, LV_PSRAM_CAPS);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc(p, new_size, LV_PSRAM_CAPS);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    /* Not tracked here — the PSRAM heap is shared, not a private LVGL pool.
     * Query the SPIRAM heap directly via heap_caps_get_info() if needed. */
    LV_UNUSED(mon_p);
    return;
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK; /* Integrity is the ESP heap allocator's concern. */
}

#endif /*LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM*/
