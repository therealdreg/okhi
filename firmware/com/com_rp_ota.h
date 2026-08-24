#ifndef _COM_RP_OTA_H__
#define _COM_RP_OTA_H__

/*
 * RP side of the okhi SPI v3 control protocol and of the RP self update.
 *
 * Shared by firmware/usb/rp/okhi.c and firmware/ps2/rp/okhi.c so the two
 * variants cannot drift apart. The ESP side is shared the same way in
 * firmware/com/com_esp.h, and both ends must agree on SPI_FRAME_VERSION.
 *
 * The board pin map, the SPI wrappers and the flash helpers come from
 * com_rp_hw.h, which this header pulls in. Include this AFTER the variant
 * okhi.c has provided:
 *
 *     RP_VARIANT                     OKHI_VARIANT_USB or OKHI_VARIANT_PS2
 *     FIRMV, FIRMV_STR               from last_firmv.h
 *
 * The variant must also call, in this order:
 *
 *     ota_boot_check()      very early in main(), BEFORE the watchdog is
 *                           enabled and before anything else touches flash
 *     report_flash_size()   optional, after stdio is up
 *     report_ota_state()    optional, after stdio is up
 *     poll_esp_if_due()     regularly, from whichever core owns the SPI bus
 *
 * poll_esp_if_due() is where the whole update happens: it polls, and when the
 * ESP offers an image it fetches, stages, verifies and applies it. It blocks
 * for as long as that takes, so capture stops while it runs. Call it from the
 * same core that owns the SPI bus and never from two places at once.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/pll.h"
#include "hardware/structs/pll.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "com.h"
#include "com_rp_hw.h"

#define SPI_BLOCK_ATTEMPTS 24
#define SPI_POLL_INTERVAL_US 50000
#define SPI_POLL_BACKOFF_US 500000
#define SPI_READY_TIMEOUT_US 2000

#define OTA_TRANSFER_MAX_ATTEMPTS 6

/*
 * Applying an image takes seconds, far longer than any sane watchdog period,
 * so the long loops feed it. Defaults to doing NOTHING, because that is the
 * safe default: a variant that never calls watchdog_enable() leaves the SDK's
 * internal load_value at 0, and watchdog_update() would then arm a zero length
 * timeout. A variant that DOES enable a watchdog must define this itself:
 *
 *     #define OTA_WATCHDOG_UPDATE() watchdog_update()
 */
#ifndef OTA_WATCHDOG_UPDATE
#define OTA_WATCHDOG_UPDATE() ((void)0)
#endif

#define RP_IDENTITY "v" FIRMV_STR " " RP_VARIANT " " __DATE__ " " __TIME__

#define OTA_APP_OFFSET 0x000000u
#define OTA_GOLDEN_OFFSET 0x100000u
#define OTA_STAGING_OFFSET 0x200000u
#define OTA_META_OFFSET 0x300000u
#define OTA_ESP_IMAGE_OFFSET 0x301000u
#define OTA_SLOT_SIZE 0x100000u
#define OTA_ESP_IMAGE_MAX_SIZE 0x300000u
#define OTA_MAP_END (OTA_ESP_IMAGE_OFFSET + OTA_ESP_IMAGE_MAX_SIZE)

#define OTA_META_MAGIC 0x494B4B4Fu
#define OTA_MAX_BOOT_ATTEMPTS 3

#define OTA_STATE_IDLE 0u
#define OTA_STATE_STAGED 1u
#define OTA_STATE_COMMITTED 2u
#define OTA_STATE_APPLYING 3u

typedef struct
{
    uint32_t magic;
    uint32_t state;
    uint32_t boot_attempts;
    uint32_t app_size;
    uint32_t app_crc;
    uint32_t golden_size;
    uint32_t golden_crc;
    uint32_t staging_size;
    uint32_t staging_crc;
    uint32_t meta_crc;
} ota_meta_t;

static const uint8_t spi_frame_magic[4] = {SPI_FRAME_MAGIC_BYTES};
static const uint8_t spi_ctrl_magic[4] = {SPI_CTRL_MAGIC_BYTES};

static uint8_t spi_poll_tx[SPI_FRAME_SIZE];
static uint8_t spi_poll_rx[SPI_FRAME_SIZE];
static uint32_t rp_app_crc_cached;
static uint64_t spi_next_poll_us;
static bool esp_link_up;
static int esp_link_reported = -1;
static uint8_t esp_firmware_version;
static uint32_t esp_poll_ok;
static uint32_t esp_poll_failed;
static uint32_t esp_poll_checks;
static uint32_t esp_poll_reported;
static bool esp_bench_armed;
static uint32_t esp_bench_ms;
static uint8_t esp_bench_run_id;
static uint8_t esp_bench_max_steps;
static uint16_t esp_bench_min_khz;
static uint16_t esp_bench_max_khz;
static uint8_t esp_bench_esp_state;
static uint32_t esp_bench_soak_ms;
static uint8_t esp_bench_stepdowns;
static uint8_t esp_bench_overclock;
static uint8_t esp_bench_kind;
static uint16_t esp_bench_resume_khz;
static uint8_t esp_selftest_armed;
static uint8_t esp_selftest_run;
static uint8_t esp_selftest_opts;
static uint8_t esp_selftest_blink;
static uint8_t esp_selftest_eboot;
static uint8_t esp_wifi_mode;
static uint32_t esp_wifi_ip;
static uint32_t esp_wifi_reported = 0xffffffffu;
static int esp_wifi_mode_reported = -1;
static uint32_t esp_uart_min_baud;
static uint32_t esp_uart_max_baud;
static int esp_selftest_last_run = -1;
static int esp_bench_last_run = -1;
static uint32_t bench_native_peri_hz;
static uint32_t bench_native_auxsrc;
static uint32_t bench_native_sys_hz;
static uint32_t bench_saved_pll_refdiv;
static uint32_t bench_saved_pll_fbdiv;
static uint32_t bench_saved_pll_pd1;
static uint32_t bench_saved_pll_pd2;
static uint8_t bench_result_seq;
static bool esp_rp_image_ready;
static bool esp_rp_commit_requested;
static bool esp_rp_reset_requested;
static bool esp_rp_bootsel_requested;
static uint8_t esp_selftest_probe;
static uint32_t esp_rp_image_size;
static uint32_t esp_rp_image_crc;
static uint32_t ota_failed_crc;
static uint32_t ota_failed_attempts;
static uint32_t esp_poll_timeouts;
static uint32_t esp_poll_badframes;
static uint32_t esp_badframes_reported;
static uint8_t esp_last_bad_head[16];
static uint8_t esp_last_bad_tail;
static bool esp_last_bad_silent;
static uint32_t esp_frame_offset;
static uint32_t esp_frame_offset_min = 0xffffffffu;
static uint32_t esp_frame_offset_max;
static uint32_t ota_boot_report;
static bool ota_golden_restored;

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    static const uint32_t table[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                       0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                       0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};

    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        crc = (crc >> 4) ^ table[crc & 0x0f];
        crc = (crc >> 4) ^ table[crc & 0x0f];
    }

    return crc;
}

static uint32_t crc32_buffer(const uint8_t *data, size_t len)
{
    return crc32_update(0xffffffffu, data, len) ^ 0xffffffffu;
}

static uint32_t crc32_flash_region(uint32_t offset, uint32_t size)
{
    return crc32_buffer((const uint8_t *)(XIP_BASE + offset), size);
}

static void read_flash_jedec_id(uint8_t *manufacturer, uint8_t *memory_type, uint8_t *capacity_code)
{
    uint8_t tx[4] = {0x9f, 0, 0, 0};
    uint8_t rx[4] = {0};

    uint32_t flags = save_and_disable_interrupts();
    flash_do_cmd(tx, rx, sizeof(tx));
    restore_interrupts(flags);

    *manufacturer = rx[1];
    *memory_type = rx[2];
    *capacity_code = rx[3];
}

static uint32_t get_detected_flash_size(void)
{
    uint8_t manufacturer = 0;
    uint8_t memory_type = 0;
    uint8_t capacity_code = 0;

    read_flash_jedec_id(&manufacturer, &memory_type, &capacity_code);

    if (capacity_code < 0x10 || capacity_code > 0x19)
    {
        return 0;
    }

    return 1u << capacity_code;
}

static void report_flash_size(void)
{
    uint8_t manufacturer = 0;
    uint8_t memory_type = 0;
    uint8_t capacity_code = 0;

    read_flash_jedec_id(&manufacturer, &memory_type, &capacity_code);

    uint32_t detected = get_detected_flash_size();
    uint32_t configured = (uint32_t)PICO_FLASH_SIZE_BYTES;

    printf("flash JEDEC id: %02x %02x %02x\r\n", manufacturer, memory_type, capacity_code);

    if (detected == 0)
    {
        printf("flash size detected: UNKNOWN, capacity code 0x%02x is out of range\r\n", capacity_code);
        return;
    }

    printf("flash size detected: %u bytes (%u MB)\r\n", (unsigned)detected, (unsigned)(detected / (1024 * 1024)));
    printf("flash size configured: %u bytes (%u MB)\r\n", (unsigned)configured,
           (unsigned)(configured / (1024 * 1024)));

    if (detected != configured)
    {
        printf("WARNING: flash size mismatch, fix PICO_FLASH_SIZE_BYTES in CMakeLists.txt\r\n");
    }
}

/*
 * Derived from the linker symbol rather than from the variant's own
 * get_start_free_flash_space_addr(), because this value decides how much of
 * the app gets copied into the golden slot. Note the ampersand: the symbol
 * marks an address, and reading it as a value yields a byte of the image.
 */
static uint32_t ota_app_size(void)
{
    uint32_t end = (((uint32_t)&__flash_binary_end) + (FLASH_PAGE_SIZE - 1)) & ~(uint32_t)(FLASH_PAGE_SIZE - 1);

    return end - (uint32_t)XIP_BASE;
}

static bool ota_map_fits_flash(void)
{
    uint32_t detected = get_detected_flash_size();

    return detected != 0 && OTA_MAP_END <= detected;
}

static void ota_meta_read(ota_meta_t *meta)
{
    memcpy(meta, (const void *)(XIP_BASE + OTA_META_OFFSET), sizeof(*meta));
}

static bool ota_meta_valid(const ota_meta_t *meta)
{
    if (meta->magic != OTA_META_MAGIC)
    {
        return false;
    }

    return crc32_buffer((const uint8_t *)meta, sizeof(*meta) - sizeof(meta->meta_crc)) == meta->meta_crc;
}

static void ota_meta_write(ota_meta_t *meta)
{
    static uint8_t page[FLASH_PAGE_SIZE];

    meta->magic = OTA_META_MAGIC;
    meta->meta_crc = crc32_buffer((const uint8_t *)meta, sizeof(*meta) - sizeof(meta->meta_crc));

    memset(page, 0xff, sizeof(page));
    memcpy(page, meta, sizeof(*meta));

    uint32_t flags = save_and_disable_interrupts();
    flash_range_erase(OTA_META_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(OTA_META_OFFSET, page, sizeof(page));
    restore_interrupts(flags);
}

static void ota_copy_region(uint32_t dst_offset, uint32_t src_offset, uint32_t size)
{
    static uint8_t buffer[FLASH_SECTOR_SIZE];

    uint32_t copied = 0;

    while (copied < size)
    {
        uint32_t chunk = (size - copied) < FLASH_SECTOR_SIZE ? (size - copied) : FLASH_SECTOR_SIZE;

        memcpy(buffer, (const void *)(XIP_BASE + src_offset + copied), chunk);

        if (chunk < FLASH_SECTOR_SIZE)
        {
            memset(buffer + chunk, 0xff, FLASH_SECTOR_SIZE - chunk);
        }

        OTA_WATCHDOG_UPDATE();

        uint32_t flags = save_and_disable_interrupts();
        flash_range_erase(dst_offset + copied, FLASH_SECTOR_SIZE);
        flash_range_program(dst_offset + copied, buffer, FLASH_SECTOR_SIZE);
        restore_interrupts(flags);

        copied += chunk;
    }
}

// Survives a watchdog reboot because it is never initialised, the same trick wait_20 uses
// to notice an ESP reset across the reset that follows it.
__attribute__((section(".uninitialized_data"))) uint32_t rp_bootsel_request;

#define RP_BOOTSEL_MAGIC 0xB0075E1Eu

// The web's BOOTSEL request arrives on core 1, inside the SPI poll, while core 0 is still
// running the capture with its PIO programs and DMA channels live. Calling reset_usb_boot
// from there does jump to the bootrom, the SPI link goes quiet exactly as it should, but
// the host never gets a usable device: the bootrom comes up on one core while the other is
// still driving the hardware underneath it. The physical BOOTSEL button works because it
// runs from main() on core 0, before core 1 is launched and with the PIO already torn down.
//
// So take that same road. Core 1 only records the request and reboots; the check below runs
// at the top of main(), on core 0, with core 1 not yet started and every peripheral back at
// its reset state, and enters the bootloader from there.
static void ota_bootsel_check(void)
{
    if (rp_bootsel_request != RP_BOOTSEL_MAGIC)
    {
        return;
    }

    // Cleared before the jump, so a board that is power cycled out of the bootloader comes
    // back as a keylogger instead of dropping into it again.
    rp_bootsel_request = 0;

    reset_usb_boot(0, 0);

    while (1)
    {
        tight_loop_contents();
    }
}

static void ota_boot_check(void)
{
    ota_meta_t meta;

    ota_bootsel_check();

    ota_boot_report = 0;

    if (!ota_map_fits_flash())
    {
        ota_boot_report = 1;
        return;
    }

    ota_meta_read(&meta);

    if (!ota_meta_valid(&meta))
    {
        ota_boot_report = 2;
        return;
    }

    rp_app_crc_cached = meta.app_crc;

    if (meta.state == OTA_STATE_APPLYING)
    {
        if (meta.staging_size > 0 && meta.staging_size <= OTA_SLOT_SIZE &&
            crc32_flash_region(OTA_STAGING_OFFSET, meta.staging_size) == meta.staging_crc)
        {
            ota_copy_region(OTA_APP_OFFSET, OTA_STAGING_OFFSET, meta.staging_size);

            if (crc32_flash_region(OTA_APP_OFFSET, meta.staging_size) == meta.staging_crc)
            {
                meta.state = OTA_STATE_COMMITTED;
                meta.boot_attempts = 0;
                meta.app_size = meta.staging_size;
                meta.app_crc = meta.staging_crc;
                ota_meta_write(&meta);

                rp_app_crc_cached = meta.app_crc;
                ota_boot_report = 7;

                watchdog_reboot(0, 0, 0);

                while (1)
                {
                    tight_loop_contents();
                }
            }
        }

        meta.state = OTA_STATE_COMMITTED;
        meta.boot_attempts = OTA_MAX_BOOT_ATTEMPTS;
        ota_meta_write(&meta);

        ota_boot_report = 8;

        return;
    }

    if (meta.state != OTA_STATE_COMMITTED)
    {
        ota_boot_report = 3;
        return;
    }

    meta.boot_attempts++;

    if (meta.boot_attempts > OTA_MAX_BOOT_ATTEMPTS)
    {
        if (meta.golden_size > 0 && meta.golden_size <= OTA_SLOT_SIZE &&
            crc32_flash_region(OTA_GOLDEN_OFFSET, meta.golden_size) == meta.golden_crc)
        {
            ota_copy_region(OTA_APP_OFFSET, OTA_GOLDEN_OFFSET, meta.golden_size);

            if (crc32_flash_region(OTA_APP_OFFSET, meta.golden_size) != meta.golden_crc)
            {
                ota_boot_report = 9;
                return;
            }

            meta.state = OTA_STATE_IDLE;
            meta.boot_attempts = 0;
            meta.app_size = meta.golden_size;
            meta.app_crc = meta.golden_crc;
            ota_meta_write(&meta);

            ota_golden_restored = true;
            ota_boot_report = 4;

            watchdog_reboot(0, 0, 0);
            while (1)
            {
                tight_loop_contents();
            }
        }

        ota_boot_report = 5;
        return;
    }

    ota_meta_write(&meta);
    ota_boot_report = 6;
}

static void ota_mark_healthy(void)
{
    ota_meta_t meta;

    ota_meta_read(&meta);

    if (!ota_meta_valid(&meta) || meta.state != OTA_STATE_COMMITTED)
    {
        return;
    }

    meta.state = OTA_STATE_IDLE;
    meta.boot_attempts = 0;

    ota_meta_write(&meta);

    printf("rp image CONFIRMED healthy, rollback disarmed\r\n");
}

static void ota_commit_staged_image(void)
{
    ota_meta_t meta;

    ota_meta_read(&meta);

    if (!ota_meta_valid(&meta) || meta.state != OTA_STATE_STAGED)
    {
        return;
    }

    if (meta.staging_size == 0 || meta.staging_size > OTA_SLOT_SIZE)
    {
        return;
    }

    OTA_WATCHDOG_UPDATE();

    if (crc32_flash_region(OTA_STAGING_OFFSET, meta.staging_size) != meta.staging_crc)
    {
        OTA_WATCHDOG_UPDATE();
        printf("rp commit REFUSED: staging crc does not match the metadata\r\n");
        return;
    }

    OTA_WATCHDOG_UPDATE();

    uint32_t app_size = ota_app_size();

    if (app_size == 0 || app_size > OTA_SLOT_SIZE)
    {
        printf("rp commit REFUSED: app size %u does not fit the slot\r\n", (unsigned)app_size);
        return;
    }

    printf("rp commit: backing up %u bytes to golden, capture SUSPENDED\r\n", (unsigned)app_size);

    ota_copy_region(OTA_GOLDEN_OFFSET, OTA_APP_OFFSET, app_size);

    OTA_WATCHDOG_UPDATE();

    uint32_t golden_crc = crc32_flash_region(OTA_GOLDEN_OFFSET, app_size);

    OTA_WATCHDOG_UPDATE();

    if (golden_crc != crc32_flash_region(OTA_APP_OFFSET, app_size))
    {
        OTA_WATCHDOG_UPDATE();
        printf("rp commit ABORTED: golden backup did not verify, nothing was applied\r\n");
        return;
    }

    OTA_WATCHDOG_UPDATE();

    meta.golden_size = app_size;
    meta.golden_crc = golden_crc;
    meta.state = OTA_STATE_APPLYING;
    meta.boot_attempts = 0;

    ota_meta_write(&meta);

    printf("rp commit: applying %u bytes over the app, DO NOT POWER OFF\r\n", (unsigned)meta.staging_size);

    ota_copy_region(OTA_APP_OFFSET, OTA_STAGING_OFFSET, meta.staging_size);

    OTA_WATCHDOG_UPDATE();

    if (crc32_flash_region(OTA_APP_OFFSET, meta.staging_size) != meta.staging_crc)
    {
        OTA_WATCHDOG_UPDATE();
        printf("rp commit did NOT verify, the next boot retries from staging\r\n");
        watchdog_reboot(0, 0, 0);

        while (1)
        {
            tight_loop_contents();
        }
    }

    OTA_WATCHDOG_UPDATE();

    meta.state = OTA_STATE_COMMITTED;
    meta.app_size = meta.staging_size;
    meta.app_crc = meta.staging_crc;

    ota_meta_write(&meta);

    printf("rp commit done, rebooting into the new image\r\n");

    watchdog_reboot(0, 0, 0);

    while (1)
    {
        tight_loop_contents();
    }
}

static bool wait_esp_ready(uint32_t timeout_us)
{
    uint64_t deadline = time_us_64() + timeout_us;

    while (!gpio_get(ELOG_SLAVEREADY_GPIO))
    {
        if (time_us_64() > deadline)
        {
            return false;
        }

        tight_loop_contents();
    }

    return true;
}

// Latched view of SLAVEREADY, for the self test. The ESP raises it when it arms a
// transaction and drops it when one completes, so sampling the line just after a
// transfer always catches the low half. The handshake below sees both levels for free:
// low while we are still waiting to be served, high the moment the ESP is armed.
#define ELOG_SEEN_LOW 0x01
#define ELOG_SEEN_HIGH 0x02

static volatile uint8_t elog_sticky;

static int my_spi_to_esp_xfer_blocking(const uint8_t *src, uint8_t *dst, size_t len)
{
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);

    elog_sticky |= gpio_get(ELOG_SLAVEREADY_GPIO) ? ELOG_SEEN_HIGH : ELOG_SEEN_LOW;

    if (!wait_esp_ready(SPI_READY_TIMEOUT_US))
    {
        gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
        return -1;
    }

    elog_sticky |= ELOG_SEEN_HIGH;

    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);

    return my_spi_write_read_blocking(src, dst, len);
}

static uint32_t spi_get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static void spi_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static void spi_build_control(uint8_t type, uint32_t block)
{
    memset(spi_poll_tx, 0, sizeof(spi_poll_tx));

    memcpy(spi_poll_tx, spi_ctrl_magic, sizeof(spi_ctrl_magic));
    spi_poll_tx[4] = SPI_FRAME_VERSION;
    spi_poll_tx[5] = type;
    spi_poll_tx[6] = (uint8_t)FIRMV;

    spi_poll_tx[8] = (uint8_t)block;
    spi_poll_tx[9] = (uint8_t)(block >> 8);
    spi_poll_tx[10] = (uint8_t)(block >> 16);
    spi_poll_tx[11] = (uint8_t)(block >> 24);

    spi_poll_tx[12] = (uint8_t)rp_app_crc_cached;
    spi_poll_tx[13] = (uint8_t)(rp_app_crc_cached >> 8);
    spi_poll_tx[14] = (uint8_t)(rp_app_crc_cached >> 16);
    spi_poll_tx[15] = (uint8_t)(rp_app_crc_cached >> 24);

    size_t identity_len = strlen(RP_IDENTITY);

    if (identity_len > SPI_IDENTITY_MAX - 1)
    {
        identity_len = SPI_IDENTITY_MAX - 1;
    }

    memcpy(spi_poll_tx + SPI_IDENTITY_OFFSET, RP_IDENTITY, identity_len);
}

static bool spi_frame_is_silent(const uint8_t *rx)
{
    for (size_t i = 0; i < SPI_FRAME_SIZE; ++i)
    {
        if (rx[i] != 0x00 && rx[i] != 0xff)
        {
            return false;
        }
    }

    return true;
}

static const uint8_t *spi_find_frame(const uint8_t *rx)
{
    for (size_t i = 0; i <= SPI_MAGIC_SEARCH_MAX; ++i)
    {
        if (memcmp(rx + i, spi_frame_magic, sizeof(spi_frame_magic)) == 0 && rx[i + 4] == SPI_FRAME_VERSION)
        {
            return rx + i;
        }
    }

    return NULL;
}

static void poll_esp(void)
{
    spi_build_control(SPI_CTRL_TYPE_POLL, 0);
    memset(spi_poll_rx, 0, sizeof(spi_poll_rx));

    if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
    {
        esp_link_up = false;
        esp_poll_failed++;
        esp_poll_timeouts++;
        return;
    }

    const uint8_t *head = spi_find_frame(spi_poll_rx);

    if (head == NULL)
    {
        esp_link_up = false;
        esp_poll_failed++;
        esp_poll_badframes++;
        memcpy(esp_last_bad_head, spi_poll_rx, sizeof(esp_last_bad_head));
        esp_last_bad_tail = spi_poll_rx[SPI_FRAME_SIZE - 1];
        esp_last_bad_silent = spi_frame_is_silent(spi_poll_rx);
        return;
    }

    esp_frame_offset = (uint32_t)(head - spi_poll_rx);

    if (esp_frame_offset < esp_frame_offset_min)
    {
        esp_frame_offset_min = esp_frame_offset;
    }

    if (esp_frame_offset > esp_frame_offset_max)
    {
        esp_frame_offset_max = esp_frame_offset;
    }

    if (head[5] != SPI_FRAME_TYPE_STATUS && head[5] != SPI_FRAME_TYPE_DATA)
    {
        esp_link_up = false;
        esp_poll_failed++;
        esp_poll_badframes++;
        memcpy(esp_last_bad_head, spi_poll_rx, sizeof(esp_last_bad_head));
        esp_last_bad_tail = spi_poll_rx[SPI_FRAME_SIZE - 1];
        esp_last_bad_silent = false;
        return;
    }

    esp_firmware_version = head[6];
    esp_rp_image_ready = (head[7] & SPI_FLAG_RP_IMAGE_READY) != 0;
    esp_rp_commit_requested = (head[7] & SPI_FLAG_RP_COMMIT) != 0;
    esp_rp_reset_requested = (head[7] & SPI_FLAG_RP_RESET) != 0;
    esp_rp_bootsel_requested = (head[7] & SPI_FLAG_RP_BOOTSEL) != 0;
    esp_selftest_probe = head[SPI_SELFTEST_STATUS_PROBE_OFF];
    esp_rp_image_size = spi_get_u32(head + 12);
    esp_rp_image_crc = spi_get_u32(head + 16);
    esp_bench_armed = (head[7] & SPI_FLAG_BENCH_ARMED) != 0;
    esp_bench_ms = spi_get_u32(head + 8);
    esp_bench_run_id = head[SPI_BENCH_STATUS_RUN_OFF];
    esp_bench_max_steps = head[SPI_BENCH_STATUS_STEPS_OFF];
    esp_bench_min_khz = (uint16_t)head[SPI_BENCH_STATUS_MINKHZ_OFF] |
                        (uint16_t)((uint16_t)head[SPI_BENCH_STATUS_MINKHZ_OFF + 1] << 8);
    esp_bench_max_khz = (uint16_t)head[SPI_BENCH_STATUS_MAXKHZ_OFF] |
                        (uint16_t)((uint16_t)head[SPI_BENCH_STATUS_MAXKHZ_OFF + 1] << 8);
    esp_bench_esp_state = head[SPI_BENCH_STATUS_STATE_OFF];
    esp_bench_soak_ms = spi_get_u32(head + SPI_BENCH_STATUS_SOAKMS_OFF);
    esp_bench_stepdowns = head[SPI_BENCH_STATUS_DOWN_OFF];
    esp_bench_overclock = head[SPI_BENCH_STATUS_OC_OFF];
    esp_bench_kind = head[SPI_BENCH_STATUS_KIND_OFF];
    esp_bench_resume_khz = (uint16_t)head[SPI_BENCH_STATUS_RESUME_OFF] |
                           (uint16_t)((uint16_t)head[SPI_BENCH_STATUS_RESUME_OFF + 1] << 8);
    esp_selftest_armed = (head[7] & SPI_FLAG_SELFTEST_ARMED) != 0;
    esp_selftest_run = head[SPI_SELFTEST_STATUS_RUN_OFF];
    esp_selftest_opts = head[SPI_SELFTEST_STATUS_OPTS_OFF];
    esp_selftest_blink = head[SPI_SELFTEST_STATUS_BLINK_OFF];
    esp_selftest_eboot = head[SPI_SELFTEST_STATUS_EBOOT_OFF];
    esp_wifi_mode = head[SPI_WIFI_MODE_OFF];
    esp_wifi_ip = spi_get_u32(head + SPI_WIFI_IP_OFF);
    esp_uart_min_baud = spi_get_u32(head + SPI_UART_MIN_BAUD_OFF);
    esp_uart_max_baud = spi_get_u32(head + SPI_UART_MAX_BAUD_OFF);
    esp_link_up = true;
    esp_poll_ok++;
}

static uint8_t ota_transfer_buffer[FLASH_SECTOR_SIZE];

static void ota_flash_sector(uint32_t offset, uint8_t *data, uint32_t used)
{
    if (used < FLASH_SECTOR_SIZE)
    {
        memset(data + used, 0xff, FLASH_SECTOR_SIZE - used);
    }

    uint32_t flags = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, data, FLASH_SECTOR_SIZE);
    restore_interrupts(flags);
}

static bool ota_fetch_block(uint32_t block, uint8_t *dst, uint16_t *out_len)
{
    for (int attempt = 0; attempt < SPI_BLOCK_ATTEMPTS; ++attempt)
    {
        spi_build_control(SPI_CTRL_TYPE_REQUEST_BLOCK, block);
        memset(spi_poll_rx, 0, sizeof(spi_poll_rx));

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            continue;
        }

        const uint8_t *head = spi_find_frame(spi_poll_rx);

        if (head == NULL || head[5] != SPI_FRAME_TYPE_DATA)
        {
            continue;
        }

        if (spi_get_u32(head + 20) != block)
        {
            continue;
        }

        uint16_t len = (uint16_t)head[24] | ((uint16_t)head[25] << 8);

        if (len == 0 || len > SPI_BLOCK_PAYLOAD)
        {
            continue;
        }

        memcpy(dst, head + SPI_PAYLOAD_FROM_MAGIC, len);
        *out_len = len;

        return true;
    }

    return false;
}

static void ota_receive_rp_image_if_offered(void)
{
    ota_meta_t meta;

    uint32_t size = esp_rp_image_size;
    uint32_t expected_crc = esp_rp_image_crc;

    if (size == 0 || size > OTA_SLOT_SIZE || !ota_map_fits_flash())
    {
        return;
    }

    ota_meta_read(&meta);

    if (ota_meta_valid(&meta) && meta.staging_size == size && meta.staging_crc == expected_crc)
    {
        return;
    }

    if (ota_meta_valid(&meta) && meta.app_size == size && meta.app_crc == expected_crc)
    {
        return;
    }

    // A different image starts with a clean slate. The strike counter is only cleared on
    // success, so without this a new upload inherited the previous one's failures: the first
    // time it stumbled it was already at the limit and got refused outright, and since the
    // refusal is silent the user just saw an image that never applied no matter how many
    // times they sent it.
    if (expected_crc != ota_failed_crc)
    {
        ota_failed_attempts = 0;
    }

    if (expected_crc == ota_failed_crc && ota_failed_attempts >= OTA_TRANSFER_MAX_ATTEMPTS)
    {
        return;
    }

    printf("rp image offered: %u bytes crc %08x, capture SUSPENDED\r\n", (unsigned)size, (unsigned)expected_crc);

    uint32_t crc = 0xffffffffu;
    uint32_t written = 0;
    uint32_t block = 0;
    uint32_t sector_used = 0;
    uint64_t started_us = time_us_64();

    while (written < size)
    {
        uint16_t len = 0;

        OTA_WATCHDOG_UPDATE();

        uint32_t expect = (size - written) < SPI_BLOCK_PAYLOAD ? (size - written) : SPI_BLOCK_PAYLOAD;

        if (!ota_fetch_block(block, ota_transfer_buffer + sector_used, &len) || len != expect)
        {
            OTA_WATCHDOG_UPDATE();

            ota_failed_crc = expected_crc;
            ota_failed_attempts++;

            printf("rp image transfer ABORTED at block %u (attempt %u), capture RESUMED\r\n", (unsigned)block,
                   (unsigned)ota_failed_attempts);

            return;
        }

        crc = crc32_update(crc, ota_transfer_buffer + sector_used, len);
        sector_used += len;
        written += len;
        block++;

        if (sector_used + SPI_BLOCK_PAYLOAD > FLASH_SECTOR_SIZE || written >= size)
        {
            ota_flash_sector(OTA_STAGING_OFFSET + (written - sector_used), ota_transfer_buffer, sector_used);
            sector_used = 0;
        }
    }

    OTA_WATCHDOG_UPDATE();

    crc ^= 0xffffffffu;

    uint32_t elapsed_ms = (uint32_t)((time_us_64() - started_us) / 1000);

    if (crc != expected_crc)
    {
        ota_failed_crc = expected_crc;
        ota_failed_attempts++;

        printf("rp image CRC MISMATCH: got %08x expected %08x, discarded (attempt %u), capture RESUMED\r\n",
               (unsigned)crc, (unsigned)expected_crc, (unsigned)ota_failed_attempts);

        return;
    }

    ota_failed_attempts = 0;

    ota_meta_read(&meta);

    if (!ota_meta_valid(&meta))
    {
        memset(&meta, 0, sizeof(meta));
    }

    meta.state = OTA_STATE_STAGED;
    meta.boot_attempts = 0;
    meta.staging_size = size;
    meta.staging_crc = crc;
    ota_meta_write(&meta);

    printf("rp image STAGED ok: %u bytes, %u blocks, crc %08x, %u ms, capture RESUMED\r\n", (unsigned)size,
           (unsigned)block, (unsigned)crc, (unsigned)elapsed_ms);
}

static void report_ota_state(void)
{
    static const char *reasons[] = {"not run",
                                    "MAP DOES NOT FIT THE DETECTED FLASH",
                                    "no metadata yet, first boot",
                                    "idle, nothing pending",
                                    "golden restored, rebooting",
                                    "golden unusable, staying on the current app",
                                    "committed image on trial",
                                    "interrupted commit reapplied from staging, rebooting",
                                    "interrupted commit could not be reapplied, rolling back",
                                    "GOLDEN RESTORE DID NOT VERIFY, will retry on next boot"};

    ota_meta_t meta;

    ota_meta_read(&meta);

    printf("ota map: app 0x%06x golden 0x%06x staging 0x%06x meta 0x%06x esp 0x%06x end 0x%06x\r\n", OTA_APP_OFFSET,
           OTA_GOLDEN_OFFSET, OTA_STAGING_OFFSET, OTA_META_OFFSET, OTA_ESP_IMAGE_OFFSET, OTA_MAP_END);
    printf("ota app size: %u bytes\r\n", (unsigned)ota_app_size());
    printf("ota boot check: %s\r\n", reasons[ota_boot_report <= 9 ? ota_boot_report : 0]);

    if (ota_meta_valid(&meta))
    {
        printf("ota meta: state %u attempts %u golden %u bytes staging %u bytes\r\n", (unsigned)meta.state,
               (unsigned)meta.boot_attempts, (unsigned)meta.golden_size, (unsigned)meta.staging_size);
    }
}


#define BENCH_VCO_MIN_HZ 750000000u
#define BENCH_VCO_MAX_HZ 1600000000u
#define BENCH_LADDER_MAX_DIVISOR 512u
#define BENCH_WARMUP_FRAMES 64
#define BENCH_STEP_WARMUP_OK 8
#define BENCH_RESULT_ATTEMPTS 24

typedef struct
{
    uint32_t hz;
    uint32_t peri_hz;
    uint8_t auxsrc;
    uint8_t fbdiv;
    uint8_t pd1;
    uint8_t pd2;
} bench_rate_t;

typedef struct
{
    uint32_t actual_hz;
    uint32_t target_hz;
    uint32_t frames;
    uint32_t miso_ok;
    uint32_t miso_bad;
    uint32_t xfer_fail;
    uint64_t elapsed_us;
    uint32_t stale;
    uint32_t peri_hz;
    uint16_t offset_min;
    uint16_t offset_max;
    uint32_t peer_ok;
    uint32_t peer_bad;
    uint32_t peer_tx;
    uint8_t fbdiv;
    uint8_t pd1;
    uint8_t pd2;
    uint8_t auxsrc;
    uint8_t kind;
} bench_step_stat_t;

static bench_rate_t bench_ladder[SPI_BENCH_MAX_STEPS];
static bool bench_ladder_pass[SPI_BENCH_MAX_STEPS];
static uint8_t bench_slot;
static uint8_t bench_tx_variant[SPI_BENCH_TX_VARIANTS][SPI_BENCH_MOSI_PATTERN_LEN];
static uint32_t bench_tx_variant_state[SPI_BENCH_TX_VARIANTS];
static uint32_t bench_tx_seq;
static uint32_t bench_last_miso_seq;

static void bench_uart_drain(void)
{
    uart_tx_wait_blocking(ESP_UART_ID);
#ifdef uart_default
    uart_tx_wait_blocking(uart_default);
#endif
}

static void bench_uart_reclock(void)
{
    uart_set_baudrate(ESP_UART_ID, RP_UART_BAUD);
#ifdef uart_default
    uart_set_baudrate(uart_default, PICO_DEFAULT_UART_BAUD_RATE);
#endif
}

static uint32_t bench_peri_auxsrc(void)
{
    return (clocks_hw->clk[clk_peri].ctrl & CLOCKS_CLK_PERI_CTRL_AUXSRC_BITS) >> CLOCKS_CLK_PERI_CTRL_AUXSRC_LSB;
}

static void bench_peri_to_sys(void)
{
    uint32_t sys_hz = clock_get_hz(clk_sys);

    bench_uart_drain();
    clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, sys_hz);
    bench_uart_reclock();
}

static void bench_go_safe(void)
{
    bench_peri_to_sys();
    spi_set_baudrate(SPI_ID, SPI_BAUD);
}

static void bench_save_usb_pll(void)
{
    bench_saved_pll_refdiv = pll_usb_hw->cs & PLL_CS_REFDIV_BITS;
    bench_saved_pll_fbdiv = pll_usb_hw->fbdiv_int & PLL_FBDIV_INT_BITS;
    bench_saved_pll_pd1 = (pll_usb_hw->prim & PLL_PRIM_POSTDIV1_BITS) >> PLL_PRIM_POSTDIV1_LSB;
    bench_saved_pll_pd2 = (pll_usb_hw->prim & PLL_PRIM_POSTDIV2_BITS) >> PLL_PRIM_POSTDIV2_LSB;
}

static void bench_restore_usb_pll(void)
{
    if (bench_saved_pll_refdiv == 0 || bench_saved_pll_fbdiv < 16 || bench_saved_pll_pd1 == 0 ||
        bench_saved_pll_pd2 == 0)
    {
        return;
    }

    uint32_t ref_hz = XOSC_HZ / bench_saved_pll_refdiv;

    pll_init(pll_usb, bench_saved_pll_refdiv, bench_saved_pll_fbdiv * ref_hz, bench_saved_pll_pd1,
             bench_saved_pll_pd2);
}

static void bench_apply_rate(const bench_rate_t *rate)
{
    if (rate->fbdiv != 0)
    {
        bench_peri_to_sys();
        pll_init(pll_usb, 1, (uint32_t)rate->fbdiv * XOSC_HZ, rate->pd1, rate->pd2);
    }

    bench_uart_drain();
    clock_configure_undivided(clk_peri, 0, rate->auxsrc, rate->peri_hz);
    bench_uart_reclock();
}

static bool bench_resolve_rate(uint32_t want_hz, bench_rate_t *out)
{
    bool found = false;

    out->hz = 0;

    if (want_hz == 0)
    {
        return false;
    }

    uint32_t sys_hz = clock_get_hz(clk_sys);
    uint32_t divisor = (sys_hz + want_hz - 1) / want_hz;

    if (divisor < 2)
    {
        divisor = 2;
    }

    if ((divisor & 1u) != 0)
    {
        divisor++;
    }

    if (divisor <= BENCH_LADDER_MAX_DIVISOR)
    {
        uint32_t hz = sys_hz / divisor;

        if (hz != 0 && hz <= want_hz)
        {
            out->hz = hz;
            out->peri_hz = sys_hz;
            out->auxsrc = CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS;
            out->fbdiv = 0;
            out->pd1 = 0;
            out->pd2 = 0;
            found = true;
        }
    }

    uint64_t want_pll = (uint64_t)want_hz * 2u;

    for (uint32_t pd1 = 1; pd1 <= 7; ++pd1)
    {
        for (uint32_t pd2 = 1; pd2 <= pd1; ++pd2)
        {
            uint32_t post = pd1 * pd2;
            uint64_t fbdiv = (want_pll * post) / XOSC_HZ;

            if (fbdiv > BENCH_VCO_MAX_HZ / XOSC_HZ)
            {
                fbdiv = BENCH_VCO_MAX_HZ / XOSC_HZ;
            }

            if (fbdiv * XOSC_HZ < BENCH_VCO_MIN_HZ)
            {
                continue;
            }

            uint32_t peri = ((uint32_t)fbdiv * XOSC_HZ) / post;
            uint32_t hz = peri / 2u;

            if (hz > want_hz || hz <= out->hz)
            {
                continue;
            }

            out->hz = hz;
            out->peri_hz = peri;
            out->auxsrc = CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB;
            out->fbdiv = (uint8_t)fbdiv;
            out->pd1 = (uint8_t)pd1;
            out->pd2 = (uint8_t)pd2;
            found = true;
        }
    }

    return found && out->hz != 0;
}

static void bench_build_variants(void)
{
    spi_build_control(SPI_CTRL_TYPE_BENCH, 0);
    spi_poll_tx[SPI_BENCH_MOSI_RUN_OFF] = esp_bench_run_id;

    for (uint32_t i = 0; i < SPI_BENCH_TX_VARIANTS; ++i)
    {
        spi_bench_fill_pattern(bench_tx_variant[i], SPI_BENCH_MOSI_PATTERN_LEN,
                               (uint32_t)esp_bench_run_id * 4096u + i * 7919u + 1u);

        memcpy(spi_poll_tx + SPI_BENCH_MOSI_PATTERN_OFF, bench_tx_variant[i], SPI_BENCH_MOSI_PATTERN_LEN);

        bench_tx_variant_state[i] =
            crc32_update(0xffffffffu, spi_poll_tx + SPI_BENCH_MOSI_CRC_A_OFF, SPI_BENCH_MOSI_CRC_A_LEN);
    }
}

static void bench_build_pattern_frame(uint8_t step)
{
    spi_build_control(SPI_CTRL_TYPE_BENCH, 0);

    spi_poll_tx[SPI_BENCH_MOSI_STEP_OFF] = step;
    spi_poll_tx[SPI_BENCH_MOSI_RUN_OFF] = esp_bench_run_id;
}

static void bench_next_tx_frame(void)
{
    uint32_t variant = bench_tx_seq % SPI_BENCH_TX_VARIANTS;

    bench_tx_seq++;

    memcpy(spi_poll_tx + SPI_BENCH_MOSI_PATTERN_OFF, bench_tx_variant[variant], SPI_BENCH_MOSI_PATTERN_LEN);
    spi_put_u32(spi_poll_tx + SPI_BENCH_MOSI_SEQ_OFF, bench_tx_seq);
    spi_put_u32(spi_poll_tx + SPI_BENCH_MOSI_CRC_OFF,
                crc32_update(bench_tx_variant_state[variant], spi_poll_tx + SPI_BENCH_MOSI_CRC_B_OFF,
                             SPI_BENCH_MOSI_CRC_B_LEN) ^
                    0xffffffffu);
}

static bool bench_frame_is_ours(const uint8_t *head)
{
    return head[5] == SPI_FRAME_TYPE_BENCH && head[SPI_BENCH_MISO_RUN_OFF] == esp_bench_run_id;
}

static bool bench_warmup(void)
{
    bench_build_pattern_frame(SPI_BENCH_NO_STEP);

    for (int attempt = 0; attempt < BENCH_WARMUP_FRAMES; ++attempt)
    {
        OTA_WATCHDOG_UPDATE();

        bench_next_tx_frame();
        memset(spi_poll_rx, 0, SPI_FRAME_SIZE);

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            continue;
        }

        const uint8_t *head = spi_find_frame(spi_poll_rx);

        if (head != NULL && bench_frame_is_ours(head))
        {
            return true;
        }
    }

    return false;
}

static void uart_run_step(uint32_t baud, uint32_t duration_ms, bench_step_stat_t *out);

static void bench_run_step(uint8_t step, const bench_rate_t *rate, uint32_t duration_ms, bench_step_stat_t *out)
{
    if (esp_bench_kind == SPI_BENCH_KIND_UART)
    {
        (void)step;
        uart_run_step(rate->hz, duration_ms, out);

        return;
    }

    memset(out, 0, sizeof(*out));

    out->target_hz = rate->hz;
    out->peri_hz = rate->peri_hz;
    out->fbdiv = rate->fbdiv;
    out->pd1 = rate->pd1;
    out->pd2 = rate->pd2;
    out->auxsrc = rate->auxsrc;
    out->offset_min = 0xFFFF;

    bench_apply_rate(rate);

    out->actual_hz = spi_set_baudrate(SPI_ID, rate->hz);

    // Warm the full-duplex pipeline at THIS clock before measuring. The first
    // frames after a clock change carry the worst MISO shift (section 7b), so
    // without this the FIRST step of a sweep is scored on that transient and
    // fails even when its clock is perfectly stable. NO_STEP frames are used so
    // the ESP ignores them (step >= SPI_BENCH_MAX_STEPS) and the transient never
    // lands in any step's counters. It settles in a few frames when the clock is
    // fine and simply burns the cap when it is not, which is harmless.
    bench_build_pattern_frame(SPI_BENCH_NO_STEP);

    int warm_ok = 0;

    for (int w = 0; w < BENCH_WARMUP_FRAMES && warm_ok < BENCH_STEP_WARMUP_OK; ++w)
    {
        OTA_WATCHDOG_UPDATE();

        bench_next_tx_frame();
        memset(spi_poll_rx, 0, SPI_FRAME_SIZE);

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            warm_ok = 0;
            continue;
        }

        const uint8_t *head = spi_find_frame(spi_poll_rx);

        if (head != NULL && bench_frame_is_ours(head))
        {
            warm_ok++;
        }
        else
        {
            warm_ok = 0;
        }
    }

    bench_build_pattern_frame(step);

    uint64_t start = time_us_64();
    uint64_t deadline = start + (uint64_t)duration_ms * 1000u;

    while (time_us_64() < deadline)
    {
        OTA_WATCHDOG_UPDATE();

        bench_next_tx_frame();
        memset(spi_poll_rx, 0, SPI_FRAME_SIZE);

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            out->xfer_fail++;
            continue;
        }

        out->frames++;

        const uint8_t *head = spi_find_frame(spi_poll_rx);

        if (head == NULL)
        {
            out->miso_bad++;
            continue;
        }

        uint16_t offset = (uint16_t)(head - spi_poll_rx);

        if (offset < out->offset_min)
        {
            out->offset_min = offset;
        }

        if (offset > out->offset_max)
        {
            out->offset_max = offset;
        }

        if (!bench_frame_is_ours(head))
        {
            out->stale++;

            if (head[5] == SPI_FRAME_TYPE_STATUS)
            {
                esp_bench_esp_state = head[SPI_BENCH_STATUS_STATE_OFF];
            }

            continue;
        }

        uint32_t seq = spi_get_u32(head + SPI_BENCH_MISO_SEQ_OFF);
        uint32_t want = crc32_update(0xffffffffu, head + SPI_BENCH_MISO_CRC_A_OFF, SPI_BENCH_MISO_CRC_A_LEN);

        want = crc32_update(want, head + SPI_BENCH_MISO_CRC_B_OFF, SPI_BENCH_MISO_CRC_B_LEN) ^ 0xffffffffu;

        bool good = want == spi_get_u32(head + SPI_BENCH_MISO_CRC_OFF);

        if (good && seq <= bench_last_miso_seq)
        {
            good = false;
        }

        if (seq > bench_last_miso_seq)
        {
            bench_last_miso_seq = seq;
        }

        if (good)
        {
            out->miso_ok++;
        }
        else
        {
            out->miso_bad++;
        }
    }

    out->elapsed_us = time_us_64() - start;

    if (out->offset_min == 0xFFFF)
    {
        out->offset_min = 0;
    }

    bench_go_safe();
}

static int bench_send_result(uint8_t step, uint8_t total, uint8_t flags, uint8_t phase,
                             const bench_step_stat_t *stat)
{
    if (++bench_result_seq == 0)
    {
        bench_result_seq = 1;
    }

    uint8_t seq = bench_result_seq;

    for (int attempt = 0; attempt < BENCH_RESULT_ATTEMPTS; ++attempt)
    {
        OTA_WATCHDOG_UPDATE();

        spi_build_control(SPI_CTRL_TYPE_BENCH_RESULT, 0);

        spi_poll_tx[SPI_BENCH_MOSI_STEP_OFF] = step;
        spi_poll_tx[SPI_BENCH_MOSI_RUN_OFF] = esp_bench_run_id;
        spi_poll_tx[SPI_BENCH_RESULT_FLAGS_OFF] = flags;
        spi_poll_tx[SPI_BENCH_RESULT_TOTAL_OFF] = total;
        spi_poll_tx[SPI_BENCH_RESULT_SEQ_OFF] = seq;
        spi_poll_tx[SPI_BENCH_RESULT_PHASE_OFF] = phase;

        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_ACTUAL_OFF, stat->actual_hz);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_TARGET_OFF, stat->target_hz);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_FRAMES_OFF, stat->frames);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_MISO_OK_OFF, stat->miso_ok);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_MISO_BAD_OFF, stat->miso_bad);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_XFER_FAIL_OFF, stat->xfer_fail);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_ELAPSED_MS_OFF, (uint32_t)(stat->elapsed_us / 1000u));
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_STALE_OFF, stat->stale);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_CLKPERI_OFF, stat->peri_hz);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_NATIVE_OFF, bench_native_peri_hz);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_SYSCLK_OFF, clock_get_hz(clk_sys));
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_MOSI_OK_OFF, stat->peer_ok);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_MOSI_BAD_OFF, stat->peer_bad);
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_PEER_TX_OFF, stat->peer_tx);

        spi_poll_tx[SPI_BENCH_RESULT_KIND_OFF] = stat->kind;

        spi_poll_tx[SPI_BENCH_RESULT_FBDIV_OFF] = stat->fbdiv;
        spi_poll_tx[SPI_BENCH_RESULT_PD1_OFF] = stat->pd1;
        spi_poll_tx[SPI_BENCH_RESULT_PD2_OFF] = stat->pd2;
        spi_poll_tx[SPI_BENCH_RESULT_AUXSRC_OFF] = stat->auxsrc;

        spi_poll_tx[SPI_BENCH_RESULT_OFFMIN_OFF] = (uint8_t)stat->offset_min;
        spi_poll_tx[SPI_BENCH_RESULT_OFFMIN_OFF + 1] = (uint8_t)(stat->offset_min >> 8);
        spi_poll_tx[SPI_BENCH_RESULT_OFFMAX_OFF] = (uint8_t)stat->offset_max;
        spi_poll_tx[SPI_BENCH_RESULT_OFFMAX_OFF + 1] = (uint8_t)(stat->offset_max >> 8);

        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_CRC_OFF,
                    crc32_buffer(spi_poll_tx + SPI_BENCH_RESULT_CRC_FROM, SPI_BENCH_RESULT_CRC_LEN));

        memset(spi_poll_rx, 0, SPI_FRAME_SIZE);

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            continue;
        }

        const uint8_t *head = spi_find_frame(spi_poll_rx);

        if (head == NULL)
        {
            continue;
        }

        if ((head[5] == SPI_FRAME_TYPE_BENCH || head[5] == SPI_FRAME_TYPE_UART_STATS) &&
            head[SPI_BENCH_MISO_RACK_OFF] == seq)
        {
            return head[SPI_BENCH_MISO_VERDICT_OFF] != 0 ? 1 : 0;
        }

        if (head[5] == SPI_FRAME_TYPE_STATUS && head[SPI_BENCH_STATUS_RACK_OFF] == seq)
        {
            esp_bench_esp_state = head[SPI_BENCH_STATUS_STATE_OFF];

            return head[SPI_BENCH_STATUS_VERDICT_OFF] != 0 ? 1 : 0;
        }
    }

    return -1;
}

static uint8_t uart_tx_frame[UART_BENCH_FRAME_LEN];
static uint8_t uart_rx_frame[UART_BENCH_FRAME_LEN];
static uint8_t uart_expect[UART_BENCH_PAYLOAD];
static uint8_t uart_setup_token;

static const uint32_t uart_baud_ladder[] = {9600,   19200,  38400,  57600,   74880,   115200, 230400,
                                            460800, 921600, 1000000, 1500000, 2000000, 3000000};

#define UART_BAUD_LADDER_LEN (sizeof(uart_baud_ladder) / sizeof(uart_baud_ladder[0]))

static bool uart_resolve_rate(uint32_t want_hz, bench_rate_t *out)
{
    memset(out, 0, sizeof(*out));

    for (int i = (int)UART_BAUD_LADDER_LEN - 1; i >= 0; --i)
    {
        if (uart_baud_ladder[i] <= want_hz)
        {
            out->hz = uart_baud_ladder[i];
            return true;
        }
    }

    return false;
}

static const uint8_t uart_bench_magic[4] = {UART_BENCH_MAGIC0, UART_BENCH_MAGIC1, UART_BENCH_MAGIC2,
                                            UART_BENCH_MAGIC3};

static void uart_bench_build(uint8_t *frame, uint32_t seed, uint32_t seq)
{
    memcpy(frame, uart_bench_magic, sizeof(uart_bench_magic));
    spi_put_u32(frame + UART_BENCH_SEQ_OFF, seq);
    spi_bench_fill_pattern(frame + UART_BENCH_DATA_OFF, UART_BENCH_PAYLOAD, seed + seq);
    spi_put_u32(frame + UART_BENCH_CRC_OFF, crc32_buffer(frame + UART_BENCH_CRC_FROM, UART_BENCH_CRC_LEN));
}

static const uint8_t *uart_read_stats(void)
{
    spi_build_control(SPI_CTRL_TYPE_POLL, 0);
    memset(spi_poll_rx, 0, SPI_FRAME_SIZE);

    if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
    {
        return NULL;
    }

    const uint8_t *head = spi_find_frame(spi_poll_rx);

    if (head == NULL || head[5] != SPI_FRAME_TYPE_UART_STATS)
    {
        return NULL;
    }

    return head;
}

// The self test borrows this handshake outside of a benchmark, where the bench run id
// is meaningless: the ESP only publishes it while a run is armed, so after any
// benchmark our copy reads back as zero and no longer matches. Inside a self test the
// self test run id is the one both sides agree on, so send that instead.
static bool uart_setup_selftest;

static bool uart_setup_peer(uint32_t baud, uint32_t seed, bool peer_sends)
{
    uart_setup_token++;

    for (int attempt = 0; attempt < UART_BENCH_SETUP_ATTEMPTS; ++attempt)
    {
        OTA_WATCHDOG_UPDATE();

        spi_build_control(SPI_CTRL_TYPE_UART_SETUP, 0);

        spi_poll_tx[SPI_BENCH_MOSI_RUN_OFF] = uart_setup_selftest ? esp_selftest_run : esp_bench_run_id;
        spi_put_u32(spi_poll_tx + SPI_UART_SETUP_BAUD_OFF, baud);
        spi_put_u32(spi_poll_tx + SPI_UART_SETUP_SEED_OFF, seed);
        spi_poll_tx[SPI_UART_SETUP_TOKEN_OFF] = uart_setup_token;
        spi_poll_tx[SPI_UART_SETUP_FLAGS_OFF] = peer_sends ? 1 : 0;
        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_CRC_OFF,
                    crc32_buffer(spi_poll_tx + SPI_BENCH_RESULT_CRC_FROM, SPI_BENCH_RESULT_CRC_LEN));

        memset(spi_poll_rx, 0, SPI_FRAME_SIZE);

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            continue;
        }

        const uint8_t *head = spi_find_frame(spi_poll_rx);

        if (head == NULL)
        {
            continue;
        }

        if (head[5] != SPI_FRAME_TYPE_UART_STATS)
        {
            // Tearing down, and the peer is answering as usual again: that already
            // proves it has left UART stats mode, which is all the teardown asks for.
            // Waiting for a stats frame that will never come just burns every attempt.
            if (baud == 0 && head[5] == SPI_FRAME_TYPE_STATUS)
            {
                return true;
            }

            continue;
        }

        bool ready = head[SPI_UART_STATS_READY_OFF] != 0;

        if (head[SPI_UART_STATS_TOKEN_OFF] == uart_setup_token && (baud == 0 ? !ready : ready))
        {
            return true;
        }
    }

    return false;
}

static void uart_bench_feed(uint8_t byte, uint8_t *fill, uint32_t seed, uint32_t *last_seq,
                            bench_step_stat_t *out)
{
    if (*fill < sizeof(uart_bench_magic))
    {
        if (byte == uart_bench_magic[*fill])
        {
            uart_rx_frame[(*fill)++] = byte;
            return;
        }

        out->stale++;

        if (*fill > 0)
        {
            *fill = 0;

            if (byte == uart_bench_magic[0])
            {
                uart_rx_frame[(*fill)++] = byte;
            }
        }

        return;
    }

    uart_rx_frame[(*fill)++] = byte;

    if (*fill < UART_BENCH_FRAME_LEN)
    {
        return;
    }

    *fill = 0;

    uint32_t seq = spi_get_u32(uart_rx_frame + UART_BENCH_SEQ_OFF);
    bool good = crc32_buffer(uart_rx_frame + UART_BENCH_CRC_FROM, UART_BENCH_CRC_LEN) ==
                spi_get_u32(uart_rx_frame + UART_BENCH_CRC_OFF);

    if (good)
    {
        spi_bench_fill_pattern(uart_expect, UART_BENCH_PAYLOAD, seed + seq);

        if (memcmp(uart_expect, uart_rx_frame + UART_BENCH_DATA_OFF, UART_BENCH_PAYLOAD) != 0)
        {
            good = false;
        }
    }

    if (good && seq <= *last_seq && *last_seq != 0)
    {
        good = false;
    }

    if (good)
    {
        *last_seq = seq;
        out->miso_ok++;
    }
    else
    {
        out->miso_bad++;
    }
}

static void uart_run_step(uint32_t baud, uint32_t duration_ms, bench_step_stat_t *out)
{
    memset(out, 0, sizeof(*out));

    out->kind = SPI_BENCH_KIND_UART;
    out->target_hz = baud;
    out->offset_min = 0xFFFF;

    uint32_t seed = (uint32_t)esp_bench_run_id * 2654435761u + baud;

    bool setup_ok = uart_setup_peer(baud, seed, true);

    if (!setup_ok)
    {
        // The peer may have taken the setup even though the acknowledgement never got
        // back to us, which would leave it sitting at the step baud rate with the
        // status channel unreadable. Tear down before giving up, never after.
        uart_setup_peer(0, seed, false);
        uart_set_baudrate(ESP_UART_ID, RP_UART_BAUD);
        uart_set_fifo_enabled(ESP_UART_ID, false);

        out->offset_min = 0;
        return;
    }

    uart_set_fifo_enabled(ESP_UART_ID, true);

    out->actual_hz = uart_set_baudrate(ESP_UART_ID, baud);

    while (uart_is_readable(ESP_UART_ID))
    {
        (void)uart_getc(ESP_UART_ID);
    }

    uint8_t rx_fill = 0;
    uint32_t last_seq = 0;
    uint32_t tx_seq = 1;
    uint32_t tx_left = 0;
    const uint8_t *tx_cursor = uart_tx_frame;

    uint64_t start = time_us_64();
    uint64_t deadline = start + (uint64_t)duration_ms * 1000u;

    while (time_us_64() < deadline)
    {
        OTA_WATCHDOG_UPDATE();

        if (tx_left == 0)
        {
            uart_bench_build(uart_tx_frame, seed, tx_seq);
            tx_cursor = uart_tx_frame;
            tx_left = UART_BENCH_FRAME_LEN;
        }

        while (tx_left > 0 && uart_is_writable(ESP_UART_ID))
        {
            uart_get_hw(ESP_UART_ID)->dr = *tx_cursor++;
            tx_left--;

            if (tx_left == 0)
            {
                tx_seq++;
                out->frames++;
            }
        }

        while (uart_is_readable(ESP_UART_ID))
        {
            uart_bench_feed((uint8_t)uart_get_hw(ESP_UART_ID)->dr, &rx_fill, seed, &last_seq, out);
        }
    }

    out->elapsed_us = time_us_64() - start;
    out->offset_min = 0;

    uint64_t drain = time_us_64() + 20000u;

    while (time_us_64() < drain)
    {
        OTA_WATCHDOG_UPDATE();

        while (uart_is_readable(ESP_UART_ID))
        {
            uart_bench_feed((uint8_t)uart_get_hw(ESP_UART_ID)->dr, &rx_fill, seed, &last_seq, out);
        }
    }

    // The MISO is one SPI transfer behind, and the ESP's counters only settle
    // once it has processed this step's traffic, so the first UART_STATS we read
    // still carries the counters from the start of the step (all zero). Poll a
    // few times and keep the freshest sample; the ESP's counters only grow within
    // a step, so the largest rx_ok is the most recent. Reading the first frame and
    // stopping was reporting peer_ok=0 even when the ESP had validated every frame.
    bool got_stats = false;

    for (int attempt = 0; attempt < UART_BENCH_STATS_POLLS; ++attempt)
    {
        OTA_WATCHDOG_UPDATE();

        const uint8_t *s = uart_read_stats();

        if (s == NULL)
        {
            continue;
        }

        uint32_t ok = spi_get_u32(s + SPI_UART_STATS_RX_OK_OFF);

        if (!got_stats || ok >= out->peer_ok)
        {
            got_stats = true;
            out->peer_ok = ok;
            out->peer_bad = spi_get_u32(s + SPI_UART_STATS_RX_BAD_OFF);
            out->peer_tx = spi_get_u32(s + SPI_UART_STATS_TX_OFF);
            out->xfer_fail = spi_get_u32(s + SPI_UART_STATS_RESYNC_OFF);
        }
    }

    uart_setup_peer(0, seed, false);

    uart_set_baudrate(ESP_UART_ID, RP_UART_BAUD);
    uart_set_fifo_enabled(ESP_UART_ID, false);
}

static bool uart_rp_clean(const bench_step_stat_t *stat)
{
    if (stat->frames == 0 || stat->peer_tx == 0)
    {
        return false;
    }

    if (stat->miso_bad != 0 || stat->peer_bad != 0)
    {
        return false;
    }

    // A UART step is asynchronous, so a frame or two at each edge of the window is
    // an artifact of when each side starts and stops, not link corruption. Corruption
    // is caught strictly above (miso_bad/peer_bad must be zero); here we allow a small
    // absolute edge slack on top of the per-mille loss budget so a clean link passes
    // even though the frame counts don't line up to the last frame.
    uint32_t mosi_lost = stat->frames > stat->peer_ok ? stat->frames - stat->peer_ok : 0;
    uint32_t miso_lost = stat->peer_tx > stat->miso_ok ? stat->peer_tx - stat->miso_ok : 0;

    uint32_t mosi_slack = stat->frames / 1000u;
    uint32_t miso_slack = stat->peer_tx / 1000u;

    if (mosi_slack < UART_BENCH_EDGE_SLACK)
    {
        mosi_slack = UART_BENCH_EDGE_SLACK;
    }

    if (miso_slack < UART_BENCH_EDGE_SLACK)
    {
        miso_slack = UART_BENCH_EDGE_SLACK;
    }

    if (mosi_lost > mosi_slack)
    {
        return false;
    }

    return miso_lost <= miso_slack;
}

static bool bench_rp_clean(const bench_step_stat_t *stat)
{
    if (stat->kind == SPI_BENCH_KIND_UART)
    {
        return uart_rp_clean(stat);
    }

    if (stat->frames == 0 || stat->miso_bad != 0 || stat->xfer_fail != 0)
    {
        return false;
    }

    if (stat->offset_max >= SPI_MAGIC_SEARCH_MAX)
    {
        return false;
    }

    return (stat->frames - stat->miso_ok) <= stat->frames / 1000u;
}

static void bench_stat_add(bench_step_stat_t *total, const bench_step_stat_t *part)
{
    total->kind = part->kind;
    total->peer_ok += part->peer_ok;
    total->peer_bad += part->peer_bad;
    total->peer_tx += part->peer_tx;
    total->actual_hz = part->actual_hz;
    total->target_hz = part->target_hz;
    total->peri_hz = part->peri_hz;
    total->fbdiv = part->fbdiv;
    total->pd1 = part->pd1;
    total->pd2 = part->pd2;
    total->auxsrc = part->auxsrc;
    total->frames += part->frames;
    total->miso_ok += part->miso_ok;
    total->miso_bad += part->miso_bad;
    total->xfer_fail += part->xfer_fail;
    total->stale += part->stale;
    total->elapsed_us += part->elapsed_us;

    if (part->offset_min < total->offset_min)
    {
        total->offset_min = part->offset_min;
    }

    if (part->offset_max > total->offset_max)
    {
        total->offset_max = part->offset_max;
    }
}

static void bench_log_step(const char *tag, uint8_t step, const bench_step_stat_t *stat, int verdict, bool clean)
{
    printf("spi bench %s %u: %u Hz (clk_peri %u", tag, (unsigned)step, (unsigned)stat->actual_hz,
           (unsigned)stat->peri_hz);

    if (stat->fbdiv != 0)
    {
        printf(", usb pll fbdiv %u pd %ux%u", (unsigned)stat->fbdiv, (unsigned)stat->pd1, (unsigned)stat->pd2);
    }

    printf("), %u frames, miso ok %u bad %u, stale %u, xfer fail %u, offset %u-%u of %u, esp %s, %s\r\n",
           (unsigned)stat->frames, (unsigned)stat->miso_ok, (unsigned)stat->miso_bad, (unsigned)stat->stale,
           (unsigned)stat->xfer_fail, (unsigned)stat->offset_min, (unsigned)stat->offset_max,
           (unsigned)SPI_MAGIC_SEARCH_MAX, verdict == 1 ? "clean" : "SAW ERRORS", clean ? "PASS" : "FAIL");
}

static bool bench_esp_gone(void)
{
    return esp_bench_esp_state == SPI_BENCH_STATE_ABORTED || esp_bench_esp_state == SPI_BENCH_STATE_TIMEOUT;
}

static int bench_measure(const bench_rate_t *rate, uint32_t duration_ms, uint8_t phase, const char *tag,
                         bench_step_stat_t *stat)
{
    if (bench_slot >= SPI_BENCH_MAX_STEPS)
    {
        return -1;
    }

    uint8_t step = bench_slot++;

    bench_ladder[step] = *rate;
    bench_ladder_pass[step] = false;
    bench_run_step(step, rate, duration_ms, stat);

    int verdict = bench_send_result(step, bench_slot, 0, phase, stat);

    if (verdict < 0)
    {
        printf("spi bench: the esp did not acknowledge %s %u, giving up\r\n", tag, (unsigned)step);
        return -1;
    }

    bool clean = verdict == 1 && bench_rp_clean(stat);

    bench_ladder_pass[step] = clean;

    bench_log_step(tag, step, stat, verdict, clean);

    return clean ? 1 : 0;
}

static bool bench_run_soak(uint8_t step, const bench_rate_t *rate, uint32_t soak_ms, bench_step_stat_t *total)
{
    bench_step_stat_t chunk;

    memset(total, 0, sizeof(*total));

    total->offset_min = 0xFFFF;
    total->actual_hz = rate->hz;
    total->target_hz = rate->hz;
    total->peri_hz = rate->peri_hz;
    total->fbdiv = rate->fbdiv;
    total->pd1 = rate->pd1;
    total->pd2 = rate->pd2;
    total->auxsrc = rate->auxsrc;

    if (bench_send_result(step, bench_slot, SPI_BENCH_RESULT_FLAG_SOAK_BEGIN | SPI_BENCH_RESULT_FLAG_PROGRESS,
                          SPI_BENCH_PHASE_CONFIRM, total) < 0)
    {
        printf("spi bench: the esp did not accept the confirmation start\r\n");
        return false;
    }

    printf("spi bench: confirming %u Hz for %u ms, this is the number that counts\r\n", (unsigned)rate->hz,
           (unsigned)soak_ms);

    uint32_t elapsed_ms = 0;
    uint32_t chunks = 0;
    int misses = 0;

    while (elapsed_ms < soak_ms)
    {
        uint32_t slice = soak_ms - elapsed_ms;

        if (slice > SPI_BENCH_SOAK_CHUNK_MS)
        {
            slice = SPI_BENCH_SOAK_CHUNK_MS;
        }

        bench_run_step(step, rate, slice, &chunk);
        bench_stat_add(total, &chunk);

        elapsed_ms += slice;
        chunks++;

        bool last = elapsed_ms >= soak_ms;
        int sent = bench_send_result(step, bench_slot, last ? 0 : SPI_BENCH_RESULT_FLAG_PROGRESS,
                                     SPI_BENCH_PHASE_CONFIRM, total);

        if ((chunks % 10) == 0 || last)
        {
            printf("spi bench confirm: %u/%u ms, %u frames, miso bad %u, stale %u, xfer fail %u, offset max %u\r\n",
                   (unsigned)elapsed_ms, (unsigned)soak_ms, (unsigned)total->frames, (unsigned)total->miso_bad,
                   (unsigned)total->stale, (unsigned)total->xfer_fail, (unsigned)total->offset_max);
        }

        if (last)
        {
            if (sent < 0)
            {
                printf("spi bench: the esp never took the final confirmation result\r\n");
                return false;
            }

            return sent == 1 && bench_rp_clean(total);
        }

        if (sent < 0)
        {
            if (++misses >= 3)
            {
                printf("spi bench: the esp stopped acknowledging the confirmation, stopping\r\n");
                return false;
            }
        }
        else
        {
            misses = 0;
        }

        if (bench_esp_gone())
        {
            printf("spi bench: the esp dropped out of the confirmation, stopping\r\n");
            return false;
        }
    }

    return false;
}


#define ST_SCRATCH_OFFSET 0x700000u
#define ST_TEXT_MAX SPI_SELFTEST_TEXT_MAX
#define ST_SEND_ATTEMPTS 24

typedef struct
{
    uint8_t gpio;
    const char *name;
} st_pin_t;

static const st_pin_t st_free_pins[] = {
    {0, "GPIO0"},  {1, "GPIO1"},   {2, "GPIO2"},          {3, "GPIO3"},
    {6, "GPIO6"},  {7, "GPIO7"},   {18, "GPIO18"},        {SPI_CS_PIN, "SPI CS"},
    {EBOOT_MASTERDATAREADY_GPIO, "EBOOT"},                {RP_LED_GPIO, "LED2"},
};

static const st_pin_t st_switch_pins[] = {
    {USSEL_PIN, "USSEL"},
    {USOE_PIN, "USOE"},
    {DP_SNIFF_GPIO, "sniff D+/DAT"},
    {DM_SNIFF_GPIO, "sniff D-/CLK"},
};

// The ESP keeps a pull-up on chip select and on MASTERDATAREADY, so an internal
// pull-down on this side cannot win and those two pins legitimately read high in both
// directions. Only the pull-up half is meaningful there.
#define ST_EXT_PULLUP_MASK ((1u << SPI_CS_PIN) | (1u << EBOOT_MASTERDATAREADY_GPIO))

#define ST_LINK_SPI_FRAMES 256
#define ST_LINK_UART_BAUD 460800
#define ST_LINK_UART_MS 200

#define ST_FREE_COUNT (sizeof(st_free_pins) / sizeof(st_free_pins[0]))
#define ST_SWITCH_COUNT (sizeof(st_switch_pins) / sizeof(st_switch_pins[0]))
#define ST_PIN_MAX (ST_FREE_COUNT + ST_SWITCH_COUNT)

static st_pin_t st_pins[ST_PIN_MAX];
static uint32_t st_pin_count;
static uint8_t st_seq;
static bool st_aborted;

static void st_pin_release(uint8_t gpio)
{
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_IN);
    gpio_disable_pulls(gpio);
}

// Every pin the sweep touches is photographed before the run and put back exactly as it
// was afterwards: function first and last, so a pin handed to PIO or to the UART goes
// back to its peripheral rather than being left on SIO. Releasing them to a floating
// input instead, which is what the sweep does between tests, only happens to be right
// for the pins that are idle anyway; this makes it right for all of them.
typedef struct
{
    gpio_function_t func;
    bool dir_out;
    bool level;
    bool pull_up;
    bool pull_down;
} st_pin_state_t;

static st_pin_state_t st_pin_saved[ST_PIN_MAX];

static void st_pin_snapshot(st_pin_state_t *dst, uint8_t gpio)
{
    dst->func = gpio_get_function(gpio);
    dst->dir_out = gpio_is_dir_out(gpio);
    dst->level = gpio_get_out_level(gpio);
    dst->pull_up = gpio_is_pulled_up(gpio);
    dst->pull_down = gpio_is_pulled_down(gpio);
}

static bool st_pin_matches(const st_pin_state_t *saved, uint8_t gpio)
{
    if (gpio_get_function(gpio) != saved->func || gpio_is_dir_out(gpio) != saved->dir_out ||
        gpio_is_pulled_up(gpio) != saved->pull_up || gpio_is_pulled_down(gpio) != saved->pull_down)
    {
        return false;
    }

    return !saved->dir_out || gpio_get_out_level(gpio) == saved->level;
}

static void st_pin_restore(const st_pin_state_t *saved, uint8_t gpio)
{
    gpio_put(gpio, saved->level);
    gpio_set_dir(gpio, saved->dir_out ? GPIO_OUT : GPIO_IN);
    gpio_set_pulls(gpio, saved->pull_up, saved->pull_down);
    gpio_set_function(gpio, saved->func);
}

static int st_send(uint8_t id, uint8_t status, uint32_t a, uint32_t b, const char *text, uint8_t flags)
{
    if (++st_seq == 0)
    {
        st_seq = 1;
    }

    uint8_t seq = st_seq;

    for (int attempt = 0; attempt < ST_SEND_ATTEMPTS; ++attempt)
    {
        OTA_WATCHDOG_UPDATE();

        spi_build_control(SPI_CTRL_TYPE_SELFTEST, 0);

        spi_poll_tx[SPI_BENCH_MOSI_RUN_OFF] = esp_selftest_run;
        spi_poll_tx[SPI_BENCH_RESULT_SEQ_OFF] = seq;
        spi_poll_tx[SPI_SELFTEST_ID_OFF] = id;
        spi_poll_tx[SPI_SELFTEST_STATUS_OFF] = status;
        spi_poll_tx[SPI_SELFTEST_FLAGS_OFF] = flags;

        spi_put_u32(spi_poll_tx + SPI_SELFTEST_A_OFF, a);
        spi_put_u32(spi_poll_tx + SPI_SELFTEST_B_OFF, b);

        // Copy through a cursor, never by clearing `text` itself: the ESP queues eight
        // transactions, so our acknowledgement only comes back several transfers later
        // and every row is sent more than once. Consuming the caller's pointer on the
        // first pass left every retry carrying an empty string, and that empty one is
        // what the ESP ended up storing, so no RP row ever showed its description.
        const char *cursor = text;

        for (uint32_t i = 0; i < SPI_SELFTEST_TEXT_MAX; ++i)
        {
            char c = (cursor != NULL) ? cursor[i] : 0;

            spi_poll_tx[SPI_SELFTEST_TEXT_OFF + i] = (uint8_t)c;

            if (c == 0)
            {
                cursor = NULL;
            }
        }

        spi_put_u32(spi_poll_tx + SPI_BENCH_RESULT_CRC_OFF,
                    crc32_buffer(spi_poll_tx + SPI_BENCH_RESULT_CRC_FROM, SPI_BENCH_RESULT_CRC_LEN));

        memset(spi_poll_rx, 0, SPI_FRAME_SIZE);

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            continue;
        }

        const uint8_t *head = spi_find_frame(spi_poll_rx);

        if (head == NULL || head[5] != SPI_FRAME_TYPE_STATUS)
        {
            continue;
        }

        esp_selftest_eboot = head[SPI_SELFTEST_STATUS_EBOOT_OFF];

        if (head[SPI_BENCH_STATUS_RACK_OFF] == seq)
        {
            return 0;
        }
    }

    st_aborted = true;

    return -1;
}

static bool st_refresh_peer(void)
{
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        OTA_WATCHDOG_UPDATE();

        spi_build_control(SPI_CTRL_TYPE_POLL, 0);
        memset(spi_poll_rx, 0, SPI_FRAME_SIZE);

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            continue;
        }

        const uint8_t *head = spi_find_frame(spi_poll_rx);

        if (head == NULL || head[5] != SPI_FRAME_TYPE_STATUS)
        {
            continue;
        }

        esp_selftest_eboot = head[SPI_SELFTEST_STATUS_EBOOT_OFF];

        return true;
    }

    return false;
}

static void st_report_clock(uint8_t id, const char *name, uint32_t src, uint32_t want_khz, uint32_t tol_khz)
{
    char text[ST_TEXT_MAX + 8];

    uint32_t got = frequency_count_khz(src);

    snprintf(text, sizeof(text), "%s %u kHz", name, (unsigned)got);

    uint8_t status = SELFTEST_INFO;

    if (want_khz != 0)
    {
        uint32_t low = want_khz > tol_khz ? want_khz - tol_khz : 0;

        status = (got >= low && got <= want_khz + tol_khz) ? SELFTEST_PASS : SELFTEST_FAIL;
    }

    st_send(id, status, got, want_khz, text, 0);
}

static void st_run_flash(void)
{
    char text[ST_TEXT_MAX + 8];

    uint8_t manufacturer = 0;
    uint8_t memory_type = 0;
    uint8_t capacity = 0;

    read_flash_jedec_id(&manufacturer, &memory_type, &capacity);

    uint32_t id = ((uint32_t)manufacturer << 16) | ((uint32_t)memory_type << 8) | capacity;

    snprintf(text, sizeof(text), "U3 jedec %02x %02x %02x", manufacturer, memory_type, capacity);
    st_send(ST_RP_FLASH_ID, manufacturer != 0x00 && manufacturer != 0xff ? SELFTEST_PASS : SELFTEST_FAIL, id, 0,
            text, 0);

    uint32_t detected = get_detected_flash_size();
    uint32_t configured = (uint32_t)PICO_FLASH_SIZE_BYTES;

    snprintf(text, sizeof(text), "%u MB, build wants %u MB", (unsigned)(detected / (1024 * 1024)),
             (unsigned)(configured / (1024 * 1024)));
    st_send(ST_RP_FLASH_SIZE, detected == configured ? SELFTEST_PASS : SELFTEST_FAIL, detected, configured, text, 0);

    if ((esp_selftest_opts & SELFTEST_OPT_FLASH) == 0)
    {
        st_send(ST_RP_FLASH_RW, SELFTEST_SKIP, 0, 0, "not requested", 0);
        return;
    }

    for (uint32_t i = 0; i < FLASH_SECTOR_SIZE; ++i)
    {
        ota_transfer_buffer[i] = (uint8_t)(i * 7u + esp_selftest_run * 31u + 0x5Au);
    }

    OTA_WATCHDOG_UPDATE();
    ota_flash_sector(ST_SCRATCH_OFFSET, ota_transfer_buffer, FLASH_SECTOR_SIZE);
    OTA_WATCHDOG_UPDATE();

    const uint8_t *back = (const uint8_t *)(XIP_BASE + ST_SCRATCH_OFFSET);
    uint32_t wrong = 0;

    for (uint32_t i = 0; i < FLASH_SECTOR_SIZE; ++i)
    {
        if (back[i] != ota_transfer_buffer[i])
        {
            wrong++;
        }
    }

    snprintf(text, sizeof(text), "4k at 0x%06x, %u bad", (unsigned)ST_SCRATCH_OFFSET, (unsigned)wrong);
    st_send(ST_RP_FLASH_RW, wrong == 0 ? SELFTEST_PASS : SELFTEST_FAIL, wrong, FLASH_SECTOR_SIZE, text, 0);
}

static void st_run_analog(void)
{
    char text[ST_TEXT_MAX + 8];

    adc_init();

    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    // The bandgap reference needs longer than a couple of milliseconds to settle after the
    // sensor is switched on, and a single sample lands wherever the noise happens to be.
    sleep_ms(20);

    uint32_t sum = 0;

    for (uint32_t i = 0; i < 16; ++i)
    {
        sum += adc_read();
        sleep_us(200);
    }

    uint32_t raw = sum / 16;
    int32_t milli = (int32_t)((uint64_t)raw * 3300u / 4096u);

    // 64 bit intermediate: milli is in mV, so (milli * 1000 - 706000) * 1000 overflows a
    // signed 32 bit int once milli passes about 2853, and a sensor reading anywhere near
    // the top of the range would then produce a wrapped, meaningless temperature rather
    // than an out of range one that the verdict below could catch.
    int32_t temp = 27000 - (int32_t)((((int64_t)milli * 1000) - 706000) * 1000 / 1721);

    snprintf(text, sizeof(text), "die %d.%01u C", (int)(temp / 1000), (unsigned)((temp < 0 ? -temp : temp) % 1000) / 100);
    st_send(ST_RP_TEMP, temp > -10000 && temp < 90000 ? SELFTEST_PASS : SELFTEST_WARN, raw, (uint32_t)temp, text, 0);

    adc_set_temp_sensor_enabled(false);

#ifdef RP_ADC_GPIO
    adc_gpio_init(RP_ADC_GPIO);
    adc_select_input(1);
    sleep_ms(2);

    raw = adc_read();
    milli = (int32_t)((uint64_t)raw * 3300u / 4096u);

    snprintf(text, sizeof(text), "GPIO27 %u mV, raw %u", (unsigned)milli, (unsigned)raw);
    st_send(ST_RP_ADC_PIN, SELFTEST_INFO, raw, (uint32_t)milli, text, 0);
#else
    st_send(ST_RP_ADC_PIN, SELFTEST_SKIP, 0, 0, "not fitted on this variant", 0);
#endif
}

static void st_run_gpio(void)
{
    char text[ST_TEXT_MAX + 8];

    uint32_t pull_bad = 0;
    uint32_t drive_bad = 0;
    uint32_t short_bad = 0;
    const char *pull_first = "";
    const char *drive_first = "";
    char short_first[ST_TEXT_MAX + 8];

    short_first[0] = '\0';

    for (uint32_t i = 0; i < st_pin_count; ++i)
    {
        uint8_t g = st_pins[i].gpio;

        OTA_WATCHDOG_UPDATE();

        gpio_init(g);
        gpio_set_dir(g, GPIO_IN);
        gpio_pull_up(g);
        sleep_us(200);

        bool high = gpio_get(g);

        gpio_pull_down(g);
        sleep_us(200);

        bool low = gpio_get(g);

        bool pull_down_matters = ((1u << g) & ST_EXT_PULLUP_MASK) == 0;

        if (!high || (low && pull_down_matters))
        {
            pull_bad |= 1u << g;

            if (pull_first[0] == '\0')
            {
                pull_first = st_pins[i].name;
            }
        }

        gpio_disable_pulls(g);
        gpio_set_dir(g, GPIO_OUT);
        gpio_put(g, 1);
        sleep_us(200);

        bool drove_high = gpio_get(g);

        gpio_put(g, 0);
        sleep_us(200);

        bool drove_low = gpio_get(g);

        if (!drove_high || drove_low)
        {
            drive_bad |= 1u << g;

            if (drive_first[0] == '\0')
            {
                drive_first = st_pins[i].name;
            }
        }

        gpio_set_dir(g, GPIO_IN);
        gpio_pull_up(g);
    }

    sleep_us(500);

    for (uint32_t i = 0; i < st_pin_count; ++i)
    {
        uint8_t g = st_pins[i].gpio;

        OTA_WATCHDOG_UPDATE();

        // Driving the mux enable low turns the USB switch on, which ties the sniff pair
        // to the host side and looks exactly like a solder bridge. Leave it pulled high
        // so the switch stays off for the whole scan; its own drive is covered above and
        // by the dedicated mux test.
        if (g == USOE_PIN)
        {
            continue;
        }

        gpio_disable_pulls(g);
        gpio_set_dir(g, GPIO_OUT);
        gpio_put(g, 0);
        sleep_us(300);

        for (uint32_t j = 0; j < st_pin_count; ++j)
        {
            if (j == i)
            {
                continue;
            }

            if (!gpio_get(st_pins[j].gpio))
            {
                short_bad |= (1u << g) | (1u << st_pins[j].gpio);

                if (short_first[0] == '\0')
                {
                    snprintf(short_first, sizeof(short_first), "%s to %s", st_pins[i].name, st_pins[j].name);
                }
            }
        }

        gpio_set_dir(g, GPIO_IN);
        gpio_pull_up(g);
        sleep_us(300);
    }

    for (uint32_t i = 0; i < st_pin_count; ++i)
    {
        st_pin_release(st_pins[i].gpio);
    }

    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
    gpio_put(SPI_CS_PIN, true);

    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_OUT);
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);

    snprintf(text, sizeof(text), "%u pins, %s", (unsigned)st_pin_count, pull_bad ? pull_first : "all follow");
    st_send(ST_RP_GPIO_PULL, pull_bad ? SELFTEST_FAIL : SELFTEST_PASS, pull_bad, st_pin_count, text, 0);

    snprintf(text, sizeof(text), "%u pins, %s", (unsigned)st_pin_count, drive_bad ? drive_first : "all drive");
    st_send(ST_RP_GPIO_DRIVE, drive_bad ? SELFTEST_FAIL : SELFTEST_PASS, drive_bad, st_pin_count, text, 0);

    snprintf(text, sizeof(text), "%s", short_bad ? short_first : "no bridges found");
    st_send(ST_RP_GPIO_SHORT, short_bad ? SELFTEST_FAIL : SELFTEST_PASS, short_bad, st_pin_count, text, 0);
}

static void st_run_chip_pu(void)
{
    char text[ST_TEXT_MAX + 8];

    gpio_init(ESP_RESET_GPIO);
    gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
    gpio_disable_pulls(ESP_RESET_GPIO);
    sleep_us(500);

    bool floating = gpio_get(ESP_RESET_GPIO);

    gpio_pull_down(ESP_RESET_GPIO);
    sleep_us(500);

    bool pulled = gpio_get(ESP_RESET_GPIO);

    gpio_disable_pulls(ESP_RESET_GPIO);
    gpio_pull_up(ESP_RESET_GPIO);

    snprintf(text, sizeof(text), "R7 holds it %s", pulled ? "up" : "NOT up");
    st_send(ST_X_ESP_RESET, floating && pulled ? SELFTEST_PASS : SELFTEST_FAIL, floating ? 1 : 0, pulled ? 1 : 0,
            text, 0);
}

// Neither handshake line can be probed by holding a level and then asking the far side,
// because the only way to ask is an SPI transfer and every transfer drives both lines
// itself: the question always arrives after the answer has been overwritten. Both sides
// therefore latch the levels they see while handshaking, and these two tests just run
// traffic and read the latch.
static void st_run_eboot(void)
{
    char text[ST_TEXT_MAX + 8];

    esp_selftest_eboot = 0;

    for (int i = 0; i < 16; ++i)
    {
        OTA_WATCHDOG_UPDATE();
        st_refresh_peer();
    }

    bool saw_high = (esp_selftest_eboot & EBOOT_SEEN_HIGH) != 0;
    bool saw_low = (esp_selftest_eboot & EBOOT_SEEN_LOW) != 0;

    snprintf(text, sizeof(text), "RP14 to ESP IO9 %s", saw_high && saw_low ? "toggles" : "STUCK");
    st_send(ST_X_EBOOT, saw_high && saw_low ? SELFTEST_PASS : SELFTEST_FAIL, saw_high ? 1 : 0, saw_low ? 1 : 0,
            text, 0);
}

static void st_run_elog(void)
{
    char text[ST_TEXT_MAX + 8];

    elog_sticky = 0;

    for (int i = 0; i < 64; ++i)
    {
        OTA_WATCHDOG_UPDATE();
        st_refresh_peer();
    }

    bool saw_high = (elog_sticky & ELOG_SEEN_HIGH) != 0;
    bool saw_low = (elog_sticky & ELOG_SEEN_LOW) != 0;

    snprintf(text, sizeof(text), "ESP IO8 to RP15 %s", saw_high && saw_low ? "toggles" : "STUCK");
    st_send(ST_X_ELOG, saw_high && saw_low ? SELFTEST_PASS : SELFTEST_FAIL, saw_high ? 1 : 0, saw_low ? 1 : 0,
            text, 0);
}

// Both links are checked the way the benchmark checks them, by moving real frames and
// verifying what comes back, rather than by inspecting a status word. A burst that
// arrives intact exercises the whole path at once: clock, both data lines, chip select
// and the two handshake wires for SPI, and the two UART wires for the other.
static void st_run_link_spi(void)
{
    char text[ST_TEXT_MAX + 8];

    uint32_t ok = 0;
    uint32_t bad = 0;
    uint32_t worst_offset = 0;

    for (uint32_t i = 0; i < ST_LINK_SPI_FRAMES; ++i)
    {
        OTA_WATCHDOG_UPDATE();

        spi_build_control(SPI_CTRL_TYPE_POLL, 0);
        memset(spi_poll_rx, 0, SPI_FRAME_SIZE);

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            bad++;
            continue;
        }

        const uint8_t *head = spi_find_frame(spi_poll_rx);

        if (head == NULL || head[4] != SPI_FRAME_VERSION)
        {
            bad++;
            continue;
        }

        uint32_t offset = (uint32_t)(head - spi_poll_rx);

        if (offset > worst_offset)
        {
            worst_offset = offset;
        }

        ok++;
    }

    // The reply always lands a fixed distance into the window, because the slave is
    // one transfer behind; what matters is that the magic is found well inside the
    // search window, not that it sits at the nominal lead.
    snprintf(text, sizeof(text), "%u frames, %u bad, offset %u", (unsigned)ok, (unsigned)bad,
             (unsigned)worst_offset);
    st_send(ST_X_SPI_LINK,
            bad == 0 && ok == ST_LINK_SPI_FRAMES && worst_offset < SPI_MAGIC_SEARCH_MAX ? SELFTEST_PASS
                                                                                        : SELFTEST_FAIL,
            ok, bad, text, 0);
}

static void st_run_link_uart(void)
{
    char text[ST_TEXT_MAX + 8];
    bench_step_stat_t stat;

    uart_setup_selftest = true;

    uart_run_step(ST_LINK_UART_BAUD, ST_LINK_UART_MS, &stat);

    // One retry when the step never got off the ground. Arming it needs a setup frame
    // to be acknowledged, and the reply is several transfers behind, so a run started
    // right after a reboot can miss the window entirely and measure nothing. A link
    // that is genuinely broken still reports nothing the second time.
    if (stat.frames == 0 || stat.peer_tx == 0)
    {
        uart_run_step(ST_LINK_UART_BAUD, ST_LINK_UART_MS, &stat);
    }

    uart_setup_selftest = false;

    bool ok = uart_rp_clean(&stat);

    snprintf(text, sizeof(text), "%u baud, tx %u rx %u", (unsigned)stat.actual_hz, (unsigned)stat.frames,
             (unsigned)stat.miso_ok);
    st_send(ST_X_UART_LINK, ok ? SELFTEST_PASS : SELFTEST_FAIL, stat.peer_ok, stat.miso_ok, text, 0);
}

// Everything in the probe below is a settling time, not a measurement, so it costs a tenth
// of a second in total against a ten second timeout. Nothing is gained by keeping it tight
// and a marginal board is exactly what this is meant to catch.
#define ST_SW_SETTLE_MS 25
#define ST_SW_SAMPLES 16

// A single gpio_get reports whatever the line was doing at that one instant. Sampling it
// over a millisecond and taking the majority answers the question that is actually being
// asked, which is where the line settles, not where it was caught. A line that cannot make
// up its mind comes back high, so it reads as not grounded and the probe reports a problem
// rather than hiding one.
static bool st_read_settled(uint8_t pin)
{
    uint32_t high = 0;

    for (uint32_t i = 0; i < ST_SW_SAMPLES; ++i)
    {
        if (gpio_get(pin))
        {
            high++;
        }

        sleep_us(100);
    }

    return high > (ST_SW_SAMPLES / 2);
}

// Interactive proof that the USB switch really switches, for the boards that carry one.
// The web walks the user through it one wire at a time: hold the sniff line up with the
// internal pull up, have a dupont wire ground the matching pin on the sniff header, and
// watch what the line does as the switch is closed and opened. Doing the two lines
// separately is what makes it catch a solder bridge between them, since the neighbour
// has to stay up while this one is grounded.
static void st_run_sw_probe(uint8_t which)
{
    char text[ST_TEXT_MAX + 8];

    bool dp = which != 2;
    uint8_t pin = dp ? DP_SNIFF_GPIO : DM_SNIFF_GPIO;
    uint8_t other = dp ? DM_SNIFF_GPIO : DP_SNIFF_GPIO;

    st_pin_state_t saved_pin;
    st_pin_state_t saved_other;
    st_pin_state_t saved_oe;

    st_pin_snapshot(&saved_pin, pin);
    st_pin_snapshot(&saved_other, other);
    st_pin_snapshot(&saved_oe, USOE_PIN);

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);

    gpio_init(other);
    gpio_set_dir(other, GPIO_IN);
    gpio_pull_up(other);

    // The internal pull up is weak, so give both lines time to actually reach the rail
    // through whatever the dupont wire and the header add before the switch is touched.
    sleep_ms(ST_SW_SETTLE_MS);

    // gpio_init leaves the pin an input, so driving OE only after set_dir would let the
    // switch float for a moment on every measurement. Park the level first, then drive.
    gpio_put(USOE_PIN, 1);
    gpio_init(USOE_PIN);
    gpio_set_dir(USOE_PIN, GPIO_OUT);
    sleep_ms(ST_SW_SETTLE_MS);

    gpio_put(USOE_PIN, 0);
    sleep_ms(ST_SW_SETTLE_MS);

    bool closed_pin = st_read_settled(pin);
    bool closed_other = st_read_settled(other);

    gpio_put(USOE_PIN, 1);
    sleep_ms(ST_SW_SETTLE_MS);

    bool open_pin = st_read_settled(pin);
    bool open_other = st_read_settled(other);

    st_pin_restore(&saved_oe, USOE_PIN);
    st_pin_restore(&saved_other, other);
    st_pin_restore(&saved_pin, pin);

    uint32_t bits = (closed_pin ? 1u : 0u) | (closed_other ? 2u : 0u) | (open_pin ? 4u : 0u) |
                    (open_other ? 8u : 0u) | (gpio_get_out_level(USSEL_PIN) ? 16u : 0u);

    bool grounded = !closed_pin;
    bool neighbour_clear = closed_other;
    bool isolates = open_pin;
    bool ok = grounded && neighbour_clear && isolates;

    const char *why = !grounded          ? "no ground through it"
                      : !neighbour_clear ? "BRIDGED to its neighbour"
                      : !isolates        ? "will not open"
                                         : "opens and closes";

    snprintf(text, sizeof(text), "%s %s", dp ? "D+/DAT" : "D-/CLK", why);
    st_send(dp ? ST_RP_SW_DP : ST_RP_SW_DM, ok ? SELFTEST_PASS : SELFTEST_FAIL, bits, which, text, 0);
}

static void st_run_switch(bool sel, bool oe)
{
    char text[ST_TEXT_MAX + 8];

    if ((esp_selftest_opts & SELFTEST_OPT_SWITCH) == 0)
    {
        st_send(ST_RP_USB_MUX, SELFTEST_SKIP, 0, 0, "not requested", 0);
        st_send(ST_RP_SNIFF, SELFTEST_SKIP, 0, 0, "not requested", 0);

        return;
    }

    gpio_init(USSEL_PIN);
    gpio_set_dir(USSEL_PIN, GPIO_OUT);
    gpio_init(USOE_PIN);
    gpio_set_dir(USOE_PIN, GPIO_OUT);
    gpio_put(USOE_PIN, 1);
    sleep_ms(ST_SW_SETTLE_MS);

    uint32_t sniff_bad = 0;

    const uint8_t sniff[2] = {DP_SNIFF_GPIO, DM_SNIFF_GPIO};

    for (int i = 0; i < 2; ++i)
    {
        gpio_init(sniff[i]);
        gpio_set_dir(sniff[i], GPIO_IN);
        gpio_pull_up(sniff[i]);
        sleep_ms(ST_SW_SETTLE_MS);

        bool up = st_read_settled(sniff[i]);

        gpio_pull_down(sniff[i]);
        sleep_ms(ST_SW_SETTLE_MS);

        bool down = st_read_settled(sniff[i]);

        if (!up || down)
        {
            sniff_bad |= 1u << sniff[i];
        }

        gpio_disable_pulls(sniff[i]);
    }

    gpio_put(USOE_PIN, oe);
    gpio_put(USSEL_PIN, sel);

    snprintf(text, sizeof(text), "U1 SEL %u OE %u restored", sel ? 1u : 0u, oe ? 1u : 0u);
    st_send(ST_RP_USB_MUX, SELFTEST_PASS, sel ? 1 : 0, oe ? 1 : 0, text, 0);

    snprintf(text, sizeof(text), "%s", sniff_bad ? "a sniff line will not move" : "both lines follow");
    st_send(ST_RP_SNIFF, sniff_bad ? SELFTEST_FAIL : SELFTEST_PASS, sniff_bad, 0, text, 0);
}

static void st_blink_led(void)
{
    gpio_init(RP_LED_GPIO);
    gpio_set_dir(RP_LED_GPIO, GPIO_OUT);

    for (int i = 0; i < 12; ++i)
    {
        OTA_WATCHDOG_UPDATE();

        gpio_put(RP_LED_GPIO, i & 1);
        sleep_ms(250);
    }

    gpio_put(RP_LED_GPIO, 0);
    gpio_set_dir(RP_LED_GPIO, GPIO_IN);
}

static void ota_run_selftest(void)
{
    char text[ST_TEXT_MAX + 8];

    st_aborted = false;
    st_pin_count = 0;

    bool saved_sel = gpio_get_out_level(USSEL_PIN);
    bool saved_oe = gpio_get_out_level(USOE_PIN);

    // The two sniff pins are D+ and D- on the usb board and DATA and CLOCK on
    // the ps2 one, where the bus is held up by the internal pull ups. Both the
    // gpio sweep and the switch check drive them and end with the pulls off,
    // so the state they were found in is recorded here and put back below.
    const uint8_t saved_sniff[2] = {DP_SNIFF_GPIO, DM_SNIFF_GPIO};
    bool saved_sniff_up[2];
    bool saved_sniff_down[2];

    for (uint32_t i = 0; i < 2; ++i)
    {
        saved_sniff_up[i] = gpio_is_pulled_up(saved_sniff[i]);
        saved_sniff_down[i] = gpio_is_pulled_down(saved_sniff[i]);
    }

    for (uint32_t i = 0; i < ST_FREE_COUNT; ++i)
    {
        st_pins[st_pin_count++] = st_free_pins[i];
    }

    if ((esp_selftest_opts & SELFTEST_OPT_SWITCH) != 0)
    {
        for (uint32_t i = 0; i < ST_SWITCH_COUNT; ++i)
        {
            st_pins[st_pin_count++] = st_switch_pins[i];
        }
    }

    for (uint32_t i = 0; i < st_pin_count; ++i)
    {
        st_pin_snapshot(&st_pin_saved[i], st_pins[i].gpio);
    }

    printf("self test run %u: capture SUSPENDED, options %02x\r\n", (unsigned)esp_selftest_run,
           (unsigned)esp_selftest_opts);

    snprintf(text, sizeof(text), "v%s %s", FIRMV_STR, RP_VARIANT);
    st_send(ST_RP_FIRMWARE, SELFTEST_INFO, FIRMV, 0, text, 0);

    snprintf(text, sizeof(text), "straps read %s", hwver_name);
    st_send(ST_RP_HWVER, hwver == VERSION_UNKNOWN ? SELFTEST_WARN : SELFTEST_PASS, (uint32_t)hwver, 0, text, 0);

    st_run_flash();

    st_report_clock(ST_RP_XOSC, "U4 TCXO", CLOCKS_FC0_SRC_VALUE_XOSC_CLKSRC, 12000, 60);
    st_report_clock(ST_RP_CLK_SYS, "clk_sys", CLOCKS_FC0_SRC_VALUE_CLK_SYS, clock_get_hz(clk_sys) / 1000, 500);
    st_report_clock(ST_RP_CLK_PERI, "clk_peri", CLOCKS_FC0_SRC_VALUE_CLK_PERI, clock_get_hz(clk_peri) / 1000, 500);
    st_report_clock(ST_RP_CLK_USB, "clk_usb", CLOCKS_FC0_SRC_VALUE_CLK_USB, 48000, 500);
    st_report_clock(ST_RP_CLK_ADC, "clk_adc", CLOCKS_FC0_SRC_VALUE_CLK_ADC, 48000, 500);
    st_report_clock(ST_RP_ROSC, "rosc", CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC, 0, 0);

    st_run_analog();

    ota_meta_t meta;

    ota_meta_read(&meta);

    snprintf(text, sizeof(text), "%s", ota_meta_valid(&meta) ? "valid" : "blank or stale");
    st_send(ST_RP_OTA_META, SELFTEST_INFO, meta.state, meta.boot_attempts, text, 0);

    st_run_gpio();
    st_run_chip_pu();
    st_run_eboot();
    st_run_elog();
    st_run_link_spi();
    st_run_link_uart();
    st_run_switch(saved_sel, saved_oe);

    // Hand every pin the sweep borrowed back exactly as it was found, then reassert the
    // link pins below, whose correct running state is known regardless of what the run
    // started from. Nothing is left floating or half configured.
    for (uint32_t i = 0; i < st_pin_count; ++i)
    {
        st_pin_restore(&st_pin_saved[i], st_pins[i].gpio);
    }

    for (uint32_t i = 0; i < 2; ++i)
    {
        gpio_set_pulls(saved_sniff[i], saved_sniff_up[i], saved_sniff_down[i]);
    }

    // Prove the hand back actually happened rather than trusting it: every borrowed pin
    // is compared against its photograph, so a future test that forgets to clean up is
    // caught here instead of showing up later as a board that misbehaves after a run.
    uint32_t pin_bad = 0;

    for (uint32_t i = 0; i < st_pin_count; ++i)
    {
        if (!st_pin_matches(&st_pin_saved[i], st_pins[i].gpio))
        {
            pin_bad |= 1u << st_pins[i].gpio;
        }
    }

    gpio_init(USSEL_PIN);
    gpio_set_dir(USSEL_PIN, GPIO_OUT);
    gpio_put(USSEL_PIN, saved_sel);

    gpio_init(USOE_PIN);
    gpio_set_dir(USOE_PIN, GPIO_OUT);
    gpio_put(USOE_PIN, saved_oe);

    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
    gpio_put(SPI_CS_PIN, true);

    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_OUT);
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);

    gpio_init(ELOG_SLAVEREADY_GPIO);
    gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);
    gpio_pull_up(ELOG_SLAVEREADY_GPIO);

    snprintf(text, sizeof(text), "%u pins, %s", (unsigned)st_pin_count, pin_bad ? "NOT restored" : "all restored");
    st_send(ST_RP_PIN_STATE, pin_bad ? SELFTEST_FAIL : SELFTEST_PASS, pin_bad, st_pin_count, text, 0);

    st_send(ST_RP_LED, SELFTEST_ASK, 0, 0, "LED2, RP GPIO26", 0);
    st_send(0, SELFTEST_INFO, 0, 0, "", st_aborted ? SPI_SELFTEST_FLAG_LAST | SPI_SELFTEST_FLAG_ABORTED
                                                   : SPI_SELFTEST_FLAG_LAST);

    printf("self test done, capture RESUMED\r\n");
}

static void ota_run_bench(void)
{
    uint32_t duration_ms = esp_bench_ms;
    uint32_t coarse_steps = esp_bench_max_steps;
    uint32_t soak_ms = esp_bench_soak_ms;
    uint32_t stepdowns = esp_bench_stepdowns;

    if (duration_ms < SPI_BENCH_MIN_MS)
    {
        duration_ms = SPI_BENCH_MIN_MS;
    }

    if (duration_ms > SPI_BENCH_MAX_MS)
    {
        duration_ms = SPI_BENCH_MAX_MS;
    }

    if (coarse_steps == 0 || coarse_steps > SPI_BENCH_MAX_STEPS)
    {
        coarse_steps = SPI_BENCH_MAX_STEPS;
    }

    if (soak_ms != 0 && soak_ms < SPI_BENCH_MIN_SOAK_MS)
    {
        soak_ms = 0;
    }

    if (soak_ms > SPI_BENCH_MAX_SOAK_MS)
    {
        soak_ms = SPI_BENCH_MAX_SOAK_MS;
    }

    if (stepdowns > SPI_BENCH_MAX_STEPDOWNS)
    {
        stepdowns = SPI_BENCH_MAX_STEPDOWNS;
    }

    uint32_t min_hz = (uint32_t)esp_bench_min_khz * 1000u;
    uint32_t max_hz = (uint32_t)esp_bench_max_khz * 1000u;

    bool uart_kind = esp_bench_kind == SPI_BENCH_KIND_UART;

    bench_native_auxsrc = bench_peri_auxsrc();
    bench_native_peri_hz = clock_get_hz(clk_peri);
    bench_native_sys_hz = clock_get_hz(clk_sys);
    bench_slot = 0;

    bench_save_usb_pll();
    bench_build_variants();

    bench_tx_seq = 0;
    bench_last_miso_seq = 0;

    bool overclocked = false;

    // The overclock, not the SPI clock, is what draws the current that can brown
    // the board out, so only raise it when the run actually needs to go past
    // clk_peri / 2. On a resume that is the resume clock; on a fresh run it is
    // the top of the range. This is what makes the resilient descent converge:
    // once it drops below clk_peri / 2 it re-arms WITHOUT the overclock, the
    // draw falls back to normal, and a clock that only reset from a thermal
    // brownout now survives. clk_peri / 2 is always reachable without help.
    uint32_t oc_target_khz = esp_bench_resume_khz != 0 ? (uint32_t)esp_bench_resume_khz : esp_bench_max_khz;
    bool need_oc = esp_bench_overclock != 0 && (uint64_t)oc_target_khz * 1000ull * 2ull > bench_native_peri_hz;

    if (!uart_kind && need_oc && bench_native_sys_hz != 0)
    {
        bench_uart_drain();

        // Doubling clk_sys to 240 MHz is roughly 2x the RP2040's rated 133 MHz.
        // It runs, but at the stock 1.10 V core it is marginal and reboots at
        // random, which showed up as the reset counter climbing mid sweep. The
        // community-standard fix is to raise the core voltage for the run; 240
        // MHz is comfortable at 1.30 V (the SDK maximum) and flash stays safe
        // because at clkdiv 2 the QSPI only misbehaves past ~250 MHz. Give the
        // regulator a moment to settle before asking for the higher clock.
        vreg_set_voltage(VREG_VOLTAGE_1_30);
        sleep_ms(3);

        overclocked = set_sys_clock_khz((bench_native_sys_hz / 1000u) * 2u, false);

        bench_uart_reclock();

        if (!overclocked)
        {
            vreg_set_voltage(VREG_VOLTAGE_1_10);
        }

        printf("spi bench: system clock %s, now %u Hz, core %s\r\n",
               overclocked ? "DOUBLED for this run" : "could NOT be doubled",
               (unsigned)clock_get_hz(clk_sys), overclocked ? "1.30 V" : "1.10 V");
    }

    bench_go_safe();

    bench_step_stat_t stat;
    bench_rate_t rate;
    bench_rate_t best;

    memset(&best, 0, sizeof(best));

    printf("%s bench run %u: capture SUSPENDED, %u coarse steps of %u ms over %u-%u kHz, confirm %u ms, "
           "%u step downs, clk_sys %u Hz, clk_peri normally %u Hz\r\n",
           uart_kind ? "uart" : "spi", (unsigned)esp_bench_run_id, (unsigned)coarse_steps, (unsigned)duration_ms,
           (unsigned)esp_bench_min_khz,
           (unsigned)esp_bench_max_khz, (unsigned)soak_ms, (unsigned)stepdowns, (unsigned)clock_get_hz(clk_sys),
           (unsigned)bench_native_peri_hz);

    if (uart_kind)
    {
        min_hz = esp_uart_min_baud != 0 ? esp_uart_min_baud : uart_baud_ladder[0];
        max_hz = esp_uart_max_baud != 0 ? esp_uart_max_baud : uart_baud_ladder[UART_BAUD_LADDER_LEN - 1];

        if (min_hz < uart_baud_ladder[0])
        {
            min_hz = uart_baud_ladder[0];
        }

        if (max_hz > uart_baud_ladder[UART_BAUD_LADDER_LEN - 1])
        {
            max_hz = uart_baud_ladder[UART_BAUD_LADDER_LEN - 1];
        }

        uint32_t usable = 0;

        for (uint32_t i = 0; i < UART_BAUD_LADDER_LEN; ++i)
        {
            if (uart_baud_ladder[i] >= min_hz && uart_baud_ladder[i] <= max_hz)
            {
                usable++;
            }
        }

        if (usable == 0)
        {
            usable = 1;
        }

        if (coarse_steps > usable)
        {
            coarse_steps = usable;
        }
    }

    bool linked = uart_kind ? uart_setup_peer(0, 0, false) : bench_warmup();

    if (min_hz == 0 || max_hz < min_hz || !linked)
    {
        memset(&stat, 0, sizeof(stat));

        printf("%s bench aborted: %s\r\n", uart_kind ? "uart" : "spi",
               max_hz < min_hz ? "the requested range is empty" : "the esp never answered");

        bench_send_result(SPI_BENCH_NO_STEP, 0, SPI_BENCH_RESULT_FLAG_LAST | SPI_BENCH_RESULT_FLAG_ABORTED,
                          SPI_BENCH_PHASE_SWEEP, &stat);
    }
    else
    {
        uint32_t first_bad_hz = 0;
        uint32_t last_hz = 0;
        bool have_best = false;
        bool unwound = false;

        // Resuming after a reset that a marginal soak triggered: skip the whole
        // sweep and refine, and drop straight into confirming just below the
        // clock that browned the board out. The step-down loop then walks down
        // from here until one clock survives a full soak, so the search
        // converges across as many resets as it takes.
        if (!uart_kind && esp_bench_resume_khz != 0 &&
            bench_resolve_rate((uint32_t)esp_bench_resume_khz * 1000u, &rate) && rate.hz >= min_hz)
        {
            best = rate;
            have_best = true;

            printf("spi bench: RESUMING confirmation from %u Hz after a reset\r\n", (unsigned)best.hz);
        }
        else
        for (uint32_t i = 0; i < coarse_steps && bench_slot < SPI_BENCH_MAX_STEPS; ++i)
        {
            bool resolved;

            if (uart_kind)
            {
                uint32_t seen = 0;
                uint32_t want = 0;

                for (uint32_t k = 0; k < UART_BAUD_LADDER_LEN; ++k)
                {
                    if (uart_baud_ladder[k] < min_hz || uart_baud_ladder[k] > max_hz)
                    {
                        continue;
                    }

                    if (seen == i)
                    {
                        want = uart_baud_ladder[k];
                        break;
                    }

                    seen++;
                }

                resolved = want != 0 && uart_resolve_rate(want, &rate);
            }
            else
            {
                uint32_t span = max_hz - min_hz;
                uint32_t want =
                    coarse_steps == 1 ? max_hz : min_hz + (uint32_t)(((uint64_t)span * i) / (coarse_steps - 1));

                resolved = bench_resolve_rate(want, &rate);
            }

            if (!resolved || rate.hz < min_hz)
            {
                continue;
            }

            if (rate.hz <= last_hz)
            {
                continue;
            }

            last_hz = rate.hz;

            int mres = bench_measure(&rate, duration_ms, SPI_BENCH_PHASE_SWEEP, "step", &stat);

            if (mres < 0)
            {
                unwound = true;
                break;
            }

            if (bench_esp_gone())
            {
                unwound = true;
                break;
            }
        }

        for (uint8_t i = 0; i < bench_slot; ++i)
        {
            if (bench_ladder_pass[i] && (!have_best || bench_ladder[i].hz > best.hz))
            {
                best = bench_ladder[i];
                have_best = true;
            }
        }

        for (uint8_t i = 0; i < bench_slot; ++i)
        {
            if (!bench_ladder_pass[i] && have_best && bench_ladder[i].hz > best.hz &&
                (first_bad_hz == 0 || bench_ladder[i].hz < first_bad_hz))
            {
                first_bad_hz = bench_ladder[i].hz;
            }
        }

        if (have_best)
        {
            printf("%s bench: highest clean rate %u Hz%s\r\n", uart_kind ? "uart" : "spi", (unsigned)best.hz,
                   first_bad_hz != 0 ? ", the next one up failed" : ", nothing above it failed");
        }

        while (!uart_kind && !unwound && have_best && first_bad_hz > best.hz + SPI_BENCH_REFINE_KHZ * 1000u &&
               bench_slot < SPI_BENCH_MAX_STEPS)
        {
            uint32_t want = best.hz + (first_bad_hz - best.hz) / 2u;

            if (!bench_resolve_rate(want, &rate) || rate.hz <= best.hz || rate.hz >= first_bad_hz)
            {
                break;
            }

            int result = bench_measure(&rate, duration_ms, SPI_BENCH_PHASE_REFINE, "refine", &stat);

            if (result < 0)
            {
                unwound = true;
                break;
            }

            if (result == 1)
            {
                best = rate;
            }
            else
            {
                first_bad_hz = rate.hz;
            }

            if (bench_esp_gone())
            {
                unwound = true;
                break;
            }
        }

        bool closed = false;

        if (!unwound && have_best && soak_ms != 0)
        {
            for (uint32_t attempt = 0; attempt <= stepdowns && bench_slot < SPI_BENCH_MAX_STEPS; ++attempt)
            {
                bench_ladder[bench_slot] = best;

                uint8_t step = bench_slot++;

                if (bench_run_soak(step, &best, soak_ms, &stat))
                {
                    printf("spi bench: CONFIRMED %u Hz over %u ms, %u frames, nothing lost\r\n", (unsigned)best.hz,
                           (unsigned)soak_ms, (unsigned)stat.frames);
                    closed = true;
                    break;
                }

                if (bench_esp_gone())
                {
                    unwound = true;
                    break;
                }

                printf("spi bench: %u Hz did NOT survive the confirmation, dropping a step\r\n", (unsigned)best.hz);

                if (attempt == stepdowns || best.hz <= min_hz)
                {
                    break;
                }

                bool dropped;

                if (uart_kind)
                {
                    dropped = uart_resolve_rate(best.hz - 1u, &rate);
                }
                else
                {
                    dropped = best.hz > SPI_BENCH_REFINE_KHZ * 1000u &&
                              bench_resolve_rate(best.hz - SPI_BENCH_REFINE_KHZ * 1000u, &rate);
                }

                if (!dropped || rate.hz >= best.hz || rate.hz < min_hz)
                {
                    break;
                }

                best = rate;
            }
        }

        if (!closed)
        {
            uint8_t flags = SPI_BENCH_RESULT_FLAG_LAST;

            if (unwound)
            {
                flags |= SPI_BENCH_RESULT_FLAG_ABORTED;
            }

            memset(&stat, 0, sizeof(stat));
            bench_send_result(SPI_BENCH_NO_STEP, bench_slot, flags, SPI_BENCH_PHASE_CONFIRM, &stat);
        }
        else
        {
            memset(&stat, 0, sizeof(stat));
            bench_send_result(SPI_BENCH_NO_STEP, bench_slot, SPI_BENCH_RESULT_FLAG_LAST, SPI_BENCH_PHASE_CONFIRM,
                              &stat);
        }
    }

    bench_peri_to_sys();
    bench_restore_usb_pll();

    if (overclocked)
    {
        bench_uart_drain();
        set_sys_clock_khz(bench_native_sys_hz / 1000u, false);
        bench_uart_reclock();

        // Back to the stock clock, so drop the core voltage back to its default.
        vreg_set_voltage(VREG_VOLTAGE_1_10);
    }

    bench_uart_drain();
    clock_configure_undivided(clk_peri, 0, bench_native_auxsrc, bench_native_peri_hz);
    bench_uart_reclock();

    uint32_t restored = spi_set_baudrate(SPI_ID, SPI_BAUD);

    esp_bench_armed = false;

    printf("spi bench done, capture RESUMED, clk_sys %u Hz, clk_peri %u Hz, spi %u Hz\r\n",
           (unsigned)clock_get_hz(clk_sys), (unsigned)clock_get_hz(clk_peri), (unsigned)restored);
}

static void poll_esp_if_due(void)
{
    uint64_t now = time_us_64();

    esp_poll_checks++;

    if (now < spi_next_poll_us)
    {
        return;
    }

    poll_esp();

    spi_next_poll_us = now + (esp_link_up ? SPI_POLL_INTERVAL_US : SPI_POLL_BACKOFF_US);

    if (esp_link_up && (esp_wifi_ip != esp_wifi_reported || (int)esp_wifi_mode != esp_wifi_mode_reported))
    {
        esp_wifi_reported = esp_wifi_ip;
        esp_wifi_mode_reported = (int)esp_wifi_mode;

        static const char *modes[3] = {"access point", "client", "joining"};

        printf("esp wifi %s, address %u.%u.%u.%u\r\n", modes[esp_wifi_mode <= 2 ? esp_wifi_mode : 0],
               (unsigned)(esp_wifi_ip & 0xff), (unsigned)((esp_wifi_ip >> 8) & 0xff),
               (unsigned)((esp_wifi_ip >> 16) & 0xff), (unsigned)((esp_wifi_ip >> 24) & 0xff));
    }

    if ((int)esp_link_up != esp_link_reported)
    {
        esp_link_reported = (int)esp_link_up;

        printf("esp link %s, esp fw v%u, polls ok %u failed %u\r\n", esp_link_up ? "UP" : "DOWN",
               (unsigned)esp_firmware_version, (unsigned)esp_poll_ok, (unsigned)esp_poll_failed);
    }

    if ((esp_poll_ok + esp_poll_failed) - esp_poll_reported >= 50)
    {
        esp_poll_reported = esp_poll_ok + esp_poll_failed;

        printf("esp polls ok %u failed %u (timeout %u badframe %u), frame offset %u (min %u max %u of %u), "
               "uptime %u ms\r\n",
               (unsigned)esp_poll_ok, (unsigned)esp_poll_failed, (unsigned)esp_poll_timeouts,
               (unsigned)esp_poll_badframes, (unsigned)esp_frame_offset,
               (unsigned)(esp_frame_offset_min == 0xffffffffu ? 0 : esp_frame_offset_min),
               (unsigned)esp_frame_offset_max, (unsigned)SPI_MAGIC_SEARCH_MAX, (unsigned)(now / 1000));

        if (esp_poll_badframes != esp_badframes_reported)
        {
            esp_badframes_reported = esp_poll_badframes;

            printf("last bad frame: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                   "... tail %02x%s\r\n",
                   esp_last_bad_head[0], esp_last_bad_head[1], esp_last_bad_head[2], esp_last_bad_head[3],
                   esp_last_bad_head[4], esp_last_bad_head[5], esp_last_bad_head[6], esp_last_bad_head[7],
                   esp_last_bad_head[8], esp_last_bad_head[9], esp_last_bad_head[10], esp_last_bad_head[11],
                   esp_last_bad_head[12], esp_last_bad_head[13], esp_last_bad_head[14], esp_last_bad_head[15],
                   esp_last_bad_tail, esp_last_bad_silent ? " (esp silent, not driving miso yet)" : "");
        }
    }

    if (esp_link_up && esp_rp_bootsel_requested)
    {
        // Deliberately NOT reset_usb_boot() from here. This runs on core 1 with core 0
        // still driving the capture, and the bootrom brought up that way never becomes a
        // usable device: the link goes quiet, the host sees nothing, and the board is
        // stranded until it is power cycled. Record it and reboot, so ota_bootsel_check()
        // enters the bootloader from main() on core 0 like the physical button does.
        printf("web requested BOOTSEL, rebooting into the bootloader from core 0\r\n");
        fflush(stdout);

        rp_bootsel_request = RP_BOOTSEL_MAGIC;

        watchdog_reboot(0, 0, 0);

        while (1)
        {
            tight_loop_contents();
        }
    }

    static uint8_t rp_probe_last;

    if (esp_link_up && esp_selftest_probe != 0 && rp_probe_last == 0)
    {
        st_run_sw_probe(esp_selftest_probe);
        spi_next_poll_us = time_us_64() + SPI_POLL_INTERVAL_US;
    }

    rp_probe_last = esp_selftest_probe;


    if (esp_link_up && esp_rp_reset_requested)
    {
        printf("web requested a reset, rebooting the RP (this resets the ESP too)\r\n");
        fflush(stdout);
        watchdog_reboot(0, 0, 0);

        while (1)
        {
            tight_loop_contents();
        }
    }

    // Edge, not level. st_blink_led() blocks this poll for three seconds, so acting on the
    // level meant that a flag the ESP failed to take back turned into a permanent blink
    // that starved the capture. Every other request here is already edge triggered against
    // a _last variable; this one was the exception.
    static uint8_t rp_blink_last;

    if (esp_link_up && esp_selftest_blink != 0 && rp_blink_last == 0)
    {
        st_blink_led();
        spi_next_poll_us = time_us_64() + SPI_POLL_INTERVAL_US;
    }

    rp_blink_last = esp_link_up ? esp_selftest_blink : 0;

    if (esp_link_up && esp_selftest_armed && esp_selftest_last_run != (int)esp_selftest_run)
    {
        esp_selftest_last_run = (int)esp_selftest_run;
        ota_run_selftest();
        spi_next_poll_us = time_us_64() + SPI_POLL_INTERVAL_US;
    }

    if (esp_link_up && esp_bench_armed && esp_bench_last_run != (int)esp_bench_run_id)
    {
        esp_bench_last_run = (int)esp_bench_run_id;
        ota_run_bench();
        spi_next_poll_us = time_us_64() + SPI_POLL_INTERVAL_US;
    }

    if (esp_link_up)
    {
        ota_mark_healthy();
    }

    if (esp_link_up && esp_rp_image_ready)
    {
        ota_receive_rp_image_if_offered();
        spi_next_poll_us = time_us_64() + SPI_POLL_INTERVAL_US;
    }

    if (esp_link_up && esp_rp_commit_requested)
    {
        ota_commit_staged_image();
        spi_next_poll_us = time_us_64() + SPI_POLL_INTERVAL_US;
    }
}

/*
 * Capture loop watchdog, shared because both variants run the same shape of
 * loop: spin on a PIO FIFO, and do periodic work when it is empty. Only the FIFO
 * reads differ.
 *
 * The variant must call watchdog_enable() before entering its capture loop, and
 * define OTA_WATCHDOG_UPDATE, or the watchdog is never fed and the device
 * reboots in a loop. It must also call watchdog_disable() as the very first
 * statement of main(): ota_boot_check() runs long before watchdog_enable() and
 * feeds the watchdog while the SDK's internal load_value is still 0, which is
 * harmless only while the watchdog is disabled.
 *
 * The hardware watchdog only catches a genuinely wedged loop. It is fed from both
 * the traffic and the idle path, so a bus that simply goes quiet keeps feeding it
 * and never reboots. Silence is normal on PS/2, where the keyboard transmits only
 * on a keypress, so an idle bus is never treated as a stall.
 *
 * Feeding is throttled to one write every CAPTURE_IDLE_CHECK_SPINS turns of an
 * idle loop, so the hot path stays a FIFO test and a counter increment.
 */

#define CAPTURE_IDLE_CHECK_SPINS 4096u

static uint32_t capture_idle_spins;

static void capture_note_traffic(void)
{
    capture_idle_spins = 0;
    OTA_WATCHDOG_UPDATE();
}

static void capture_note_idle(void)
{
    if (++capture_idle_spins < CAPTURE_IDLE_CHECK_SPINS)
    {
        tight_loop_contents();
        return;
    }

    capture_idle_spins = 0;
    OTA_WATCHDOG_UPDATE();
}

#endif /* _COM_RP_OTA_H__ */
