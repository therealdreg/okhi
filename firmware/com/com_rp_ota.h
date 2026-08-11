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

#include "hardware/flash.h"
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
static bool esp_rp_image_ready;
static bool esp_rp_commit_requested;
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

static void ota_boot_check(void)
{
    ota_meta_t meta;

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

static int my_spi_to_esp_xfer_blocking(const uint8_t *src, uint8_t *dst, size_t len)
{
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);

    if (!wait_esp_ready(SPI_READY_TIMEOUT_US))
    {
        gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
        return -1;
    }

    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);

    return my_spi_write_read_blocking(src, dst, len);
}

static uint32_t spi_get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
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

    esp_firmware_version = head[6];
    esp_rp_image_ready = (head[7] & SPI_FLAG_RP_IMAGE_READY) != 0;
    esp_rp_commit_requested = (head[7] & SPI_FLAG_RP_COMMIT) != 0;
    esp_rp_image_size = spi_get_u32(head + 12);
    esp_rp_image_crc = spi_get_u32(head + 16);
    esp_bench_armed = (head[7] & SPI_FLAG_BENCH_ARMED) != 0;
    esp_bench_ms = spi_get_u32(head + 8);
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

static void ota_run_bench(uint32_t duration_ms)
{
    uint64_t deadline = time_us_64() + (uint64_t)duration_ms * 1000u;
    uint32_t frames = 0;
    uint32_t failures = 0;

    printf("spi bench: capture SUSPENDED for %u ms\r\n", (unsigned)duration_ms);

    while (time_us_64() < deadline)
    {
        OTA_WATCHDOG_UPDATE();

        if (my_spi_to_esp_xfer_blocking(spi_poll_tx, spi_poll_rx, SPI_FRAME_SIZE) < 0)
        {
            failures++;
            continue;
        }

        frames++;
    }

    OTA_WATCHDOG_UPDATE();

    uint32_t elapsed_ms = duration_ms == 0 ? 1 : duration_ms;
    uint32_t bytes = frames * SPI_FRAME_SIZE;

    printf("spi bench done: %u frames, %u failures, %u bytes in %u ms, %u KB/s, capture RESUMED\r\n",
           (unsigned)frames, (unsigned)failures, (unsigned)bytes, (unsigned)duration_ms,
           (unsigned)((bytes / 1024u) * 1000u / elapsed_ms));

    esp_bench_armed = false;
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

    if (esp_link_up && esp_bench_armed)
    {
        ota_run_bench(esp_bench_ms > 20000 ? 20000 : esp_bench_ms);
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
 * Capture loop watchdog and stall detection, shared because both variants run
 * the same shape of loop: spin on a PIO FIFO, and do periodic work when it is
 * empty. Only the FIFO reads differ.
 *
 * The variant must call watchdog_enable() before entering its capture loop, and
 * define OTA_WATCHDOG_UPDATE, or the watchdog is never fed and the device
 * reboots in a loop. It must also call watchdog_disable() as the very first
 * statement of main(): ota_boot_check() runs long before watchdog_enable() and
 * feeds the watchdog while the SDK's internal load_value is still 0, which is
 * harmless only while the watchdog is disabled.
 *
 * Feeding is throttled to one write every CAPTURE_IDLE_CHECK_SPINS turns of an
 * idle loop, so the hot path stays a FIFO test and a counter increment.
 */

#define CAPTURE_STALL_TIMEOUT_US (300u * 1000000u)
#define CAPTURE_IDLE_CHECK_SPINS 4096u

static uint32_t capture_last_word_us;
static uint32_t capture_idle_spins;
static bool capture_seen_traffic;

static void capture_note_traffic(void)
{
    capture_idle_spins = 0;
    OTA_WATCHDOG_UPDATE();

    capture_last_word_us = time_us_32();
    capture_seen_traffic = true;
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

    if (capture_seen_traffic && (time_us_32() - capture_last_word_us) > CAPTURE_STALL_TIMEOUT_US)
    {
        capture_seen_traffic = false;

        printf("capture STALLED for %u s after seeing traffic, rebooting\r\n",
               (unsigned)(CAPTURE_STALL_TIMEOUT_US / 1000000u));

        watchdog_reboot(0, 0, 0);

        while (1)
        {
            tight_loop_contents();
        }
    }
}

#endif /* _COM_RP_OTA_H__ */
