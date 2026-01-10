/*
 * Fuse Compatibility Checker
 * Based on TegraExplorer, Lockpick_RCM, and fuse-check
 *
 * Copyright (c) 2018-2025 CTCaer, shchmue, and contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <string.h>
#include <ctype.h>
#include "config.h"
#include <display/di.h>
#include <gfx_utils.h>
#include <mem/heap.h>
#include <mem/minerva.h>
#include <power/max77620.h>
#include <soc/bpmp.h>
#include <soc/fuse.h>
#include <soc/hw_init.h>
#include <soc/t210.h>
#include "storage/emummc.h"
#include "storage/nx_emmc.h"
#include <storage/nx_sd.h>
#include <storage/sdmmc.h>
#include <utils/btn.h>
#include <utils/util.h>
#include <utils/list.h>
#include <utils/sprintf.h>
#include "keys/keys.h"
#include <sec/se.h>
#include "frontend/gui.h"
#include <input/touch.h>

// Color definitions and macros (from TegraExplorer)
#define COLOR_WHITE 0xFFFFFFFF
#define COLOR_RED 0xFFFF0000
#define COLOR_GREEN 0xFF00FF00
#define COLOR_BLUE 0xFF0000FF
#define COLOR_YELLOW 0xFFFFFF00
#define COLOR_ORANGE 0xFFFFAA00
#define COLOR_CYAN 0xFF00FFFF
#define COLOR_DEFAULT 0xFF1B1B1B
#define SETCOLOR(fg, bg) gfx_con_setcol(fg, 1, bg)
#define RESETCOLOR SETCOLOR(COLOR_WHITE, COLOR_DEFAULT)

hekate_config h_cfg;
boot_cfg_t __attribute__((section ("._boot_cfg"))) b_cfg;
const volatile ipl_ver_meta_t __attribute__((section ("._ipl_version"))) ipl_ver = {
	.magic = LP_MAGIC,
	.version = (LP_VER_MJ + '0') | ((LP_VER_MN + '0') << 8) | ((LP_VER_BF + '0') << 16),
	.rsvd0 = 0,
	.rsvd1 = 0
};

volatile nyx_storage_t *nyx_str = (nyx_storage_t *)NYX_STORAGE_ADDR;
extern void pivot_stack(u32 stack_top);

// Forward declarations
bool parse_version_string(const char *version_str, u8 *major, u8 *minor, u8 *patch);
void debug_log(const char *msg);

// Fuse-to-firmware mapping (from switchbrew.org/wiki/Fuses)
typedef struct {
    u8 major_min;
    u8 minor_min;
    u8 major_max;
    u8 minor_max;
    u8 fuses_required;
} fw_fuse_map_t;

static const fw_fuse_map_t fuse_map[] = {
    {1, 0, 1, 0, 1},    // 1.0.0
    {2, 0, 2, 3, 2},    // 2.0.0-2.3.0
    {3, 0, 3, 0, 3},    // 3.0.0
    {3, 1, 3, 2, 4},    // 3.0.1-3.0.2
    {4, 0, 4, 1, 5},    // 4.0.0-4.1.0
    {5, 0, 5, 1, 6},    // 5.0.0-5.1.0
    {6, 0, 6, 1, 7},    // 6.0.0-6.1.0
    {6, 2, 6, 2, 8},    // 6.2.0
    {7, 0, 8, 1, 9},    // 7.0.0-8.0.1
    {8, 1, 8, 1, 10},   // 8.1.0
    {9, 0, 9, 1, 11},   // 9.0.0-9.0.1
    {9, 1, 9, 2, 12},   // 9.1.0-9.2.0
    {10, 0, 10, 2, 13}, // 10.0.0-10.2.0
    {11, 0, 12, 1, 14}, // 11.0.0-12.0.1
    {12, 2, 13, 1, 15}, // 12.0.2-13.1.0
    {13, 2, 14, 1, 16}, // 13.2.1-14.1.2
    {15, 0, 15, 1, 17}, // 15.0.0-15.0.1
    {16, 0, 16, 1, 18}, // 16.0.0-16.1.0
    {17, 0, 18, 1, 19}, // 17.0.0-18.1.0
    {19, 0, 19, 1, 20}, // 19.0.0-19.0.1
    {20, 0, 20, 5, 21}, // 20.0.0-20.5.0
    {21, 0, 21, 1, 22}, // 21.0.0-21.0.1
};


// Unified database (NCA + Fuse Count) loaded from SD
#define DATABASE_PATH "sd:/config/fusecheck/fusecheck_db.txt"
#define MAX_NCA_ENTRIES 256
#define MAX_FUSE_ENTRIES 64

typedef struct {
    char version[16];
    char nca_filename[64];
} nca_entry_t;

typedef struct {
    char version_range[32];
    u8 prod_fuses;
    u8 dev_fuses;
} fuse_count_entry_t;

static nca_entry_t nca_db[MAX_NCA_ENTRIES];
static size_t nca_db_count = 0;

static fuse_count_entry_t fuse_db[MAX_FUSE_ENTRIES];
static size_t fuse_db_count = 0;

static bool database_loaded = false;

static bool str_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t ls = strlen(s);
    size_t lsf = strlen(suffix);
    return (ls >= lsf) && (strncmp(s + ls - lsf, suffix, lsf) == 0);
}

static void strip_newline(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

// Unified database loader
static void load_database(void) {
    if (database_loaded)
        return;

    database_loaded = true;

    FIL fp;
    if (f_open(&fp, DATABASE_PATH, FA_READ) != FR_OK) {
        debug_log("DB: file not found, using built-in data");
        return;
    }

    char line[128];
    while (f_gets(line, sizeof(line), &fp)) {
        strip_newline(line);

        const char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;

        // Skip comments and empty lines
        if (*p == '\0' || *p == '#') continue;

        // Check for [NCA] prefix
        if (p[0] == '[' && p[1] == 'N' && p[2] == 'C' && p[3] == 'A' && p[4] == ']') {
            if (nca_db_count >= MAX_NCA_ENTRIES) continue;

            p += 5; // Skip "[NCA]"
            while (*p && isspace((unsigned char)*p)) p++;

            // Parse version
            char version[16] = {0};
            int vi = 0;
            while (*p && !isspace((unsigned char)*p) && vi < 15) {
                version[vi++] = *p++;
            }
            while (*p && isspace((unsigned char)*p)) p++;

            // Parse NCA filename
            char filename[64] = {0};
            int fi = 0;
            while (*p && !isspace((unsigned char)*p) && fi < 63) {
                filename[fi++] = *p++;
            }

            if (version[0] && filename[0] && str_ends_with(filename, ".nca")) {
                u8 tmp_maj = 0, tmp_min = 0, tmp_pat = 0;
                if (parse_version_string(version, &tmp_maj, &tmp_min, &tmp_pat)) {
                    strncpy(nca_db[nca_db_count].version, version, sizeof(nca_db[nca_db_count].version) - 1);
                    strncpy(nca_db[nca_db_count].nca_filename, filename, sizeof(nca_db[nca_db_count].nca_filename) - 1);
                    nca_db_count++;
                }
            }
        }
        // Check for [FUSE] prefix
        else if (p[0] == '[' && p[1] == 'F' && p[2] == 'U' && p[3] == 'S' && p[4] == 'E' && p[5] == ']') {
            if (fuse_db_count >= MAX_FUSE_ENTRIES) continue;

            p += 6; // Skip "[FUSE]"
            while (*p && isspace((unsigned char)*p)) p++;

            // Parse version range
            char version_range[32] = {0};
            int vi = 0;
            while (*p && !isspace((unsigned char)*p) && vi < 31) {
                version_range[vi++] = *p++;
            }
            while (*p && isspace((unsigned char)*p)) p++;

            // Parse production fuses
            int prod_fuses = 0;
            while (*p >= '0' && *p <= '9') {
                prod_fuses = prod_fuses * 10 + (*p - '0');
                p++;
            }
            while (*p && isspace((unsigned char)*p)) p++;

            // Parse dev fuses
            int dev_fuses = 0;
            while (*p >= '0' && *p <= '9') {
                dev_fuses = dev_fuses * 10 + (*p - '0');
                p++;
            }

            if (version_range[0] && prod_fuses >= 0 && prod_fuses <= 255 && dev_fuses >= 0 && dev_fuses <= 255) {
                strncpy(fuse_db[fuse_db_count].version_range, version_range, sizeof(fuse_db[fuse_db_count].version_range) - 1);
                fuse_db[fuse_db_count].prod_fuses = (u8)prod_fuses;
                fuse_db[fuse_db_count].dev_fuses = (u8)dev_fuses;
                fuse_db_count++;
            }
        }
    }

    f_close(&fp);

    char buf[64];
    s_printf(buf, "DB: loaded %d NCA, %d fuse entries", (int)nca_db_count, (int)fuse_db_count);
    debug_log(buf);
}

// Payload relocation defines
#define RELOC_META_OFF      0x7C
#define PATCHED_RELOC_SZ    0x94
#define PATCHED_RELOC_STACK 0x40007000
#define PATCHED_RELOC_ENTRY 0x40010000
#define EXT_PAYLOAD_ADDR    0xC0000000
#define RCM_PAYLOAD_ADDR    (EXT_PAYLOAD_ADDR + ALIGN(PATCHED_RELOC_SZ, 0x10))
#define COREBOOT_END_ADDR   0xD0000000
#define COREBOOT_VER_OFF    0x41
#define CBFS_DRAM_EN_ADDR   0x4003e000
#define CBFS_DRAM_MAGIC     0x4452414D // "DRAM"

static void *coreboot_addr;
static char *payload_path = NULL;

// Forward declaration
extern int nx_emmc_bis_init(emmc_part_t *part);

void reloc_patcher(u32 payload_dst, u32 payload_src, u32 payload_size) {
    memcpy((u8 *)payload_src, (u8 *)IPL_LOAD_ADDR, PATCHED_RELOC_SZ);

    volatile reloc_meta_t *relocator = (reloc_meta_t *)(payload_src + RELOC_META_OFF);

    relocator->start = payload_dst - ALIGN(PATCHED_RELOC_SZ, 0x10);
    relocator->stack = PATCHED_RELOC_STACK;
    relocator->end   = payload_dst + payload_size;
    relocator->ep    = payload_dst;

    if (payload_size == 0x7000) {
        memcpy((u8 *)(payload_src + ALIGN(PATCHED_RELOC_SZ, 0x10)), coreboot_addr, 0x7000);
        *(vu32 *)CBFS_DRAM_EN_ADDR = CBFS_DRAM_MAGIC;
    }
}

int launch_payload(char *path) {
    if (!path) return 1;

    if (sd_mount()) {
        FIL fp;
        if (f_open(&fp, path, FA_READ)) {
            return 1;
        }

        void *buf;
        u32 size = f_size(&fp);

        if (size < 0x30000)
            buf = (void *)RCM_PAYLOAD_ADDR;
        else {
            coreboot_addr = (void *)(COREBOOT_END_ADDR - size);
            buf = coreboot_addr;
            if (h_cfg.t210b01) {
                f_close(&fp);
                return 1;
            }
        }

        if (f_read(&fp, buf, size, NULL)) {
            f_close(&fp);
            return 1;
        }

        f_close(&fp);
        sd_end();

        if (size < 0x30000) {
            reloc_patcher(PATCHED_RELOC_ENTRY, EXT_PAYLOAD_ADDR, ALIGN(size, 0x10));
            hw_reinit_workaround(false, byte_swap_32(*(u32 *)(buf + size - sizeof(u32))));
        } else {
            reloc_patcher(PATCHED_RELOC_ENTRY, EXT_PAYLOAD_ADDR, 0x7000);
            u32 magic = 0;
            char *magic_ptr = buf + COREBOOT_VER_OFF;
            memcpy(&magic, magic_ptr + strlen(magic_ptr) - 4, 4);
            hw_reinit_workaround(true, magic);
        }

        // Some cards (Sandisk U1), do not like a fast power cycle. Wait min 100ms.
        sdmmc_storage_init_wait_sd();

        void (*ext_payload_ptr)() = (void *)EXT_PAYLOAD_ADDR;
        void (*update_ptr)() = (void *)RCM_PAYLOAD_ADDR;

        // Launch
        if (size < 0x30000)
            (*update_ptr)();
        else
            (*ext_payload_ptr)();
    }

    return 1;
}

u8 get_burnt_fuses() {
    u8 fuse_count = 0;
    u32 fuse_odm6 = fuse_read_odm(6);
    u32 fuse_odm7 = fuse_read_odm(7);

    for (u32 i = 0; i < 32; i++) {
        if ((fuse_odm6 >> i) & 1)
            fuse_count++;
    }

    for (u32 i = 0; i < 32; i++) {
        if ((fuse_odm7 >> i) & 1)
            fuse_count++;
    }

    return fuse_count;
}

u8 get_required_fuses(u8 major, u8 minor) {
    for (int i = 0; i < sizeof(fuse_map) / sizeof(fw_fuse_map_t); i++) {
        if (major >= fuse_map[i].major_min && major <= fuse_map[i].major_max) {
            if (major > fuse_map[i].major_min || minor >= fuse_map[i].minor_min) {
                if (major < fuse_map[i].major_max || minor <= fuse_map[i].minor_max) {
                    return fuse_map[i].fuses_required;
                }
            }
        }
    }
    return 1;
}


// Helper function to parse version string like "18.0.1" into major, minor, patch
bool parse_version_string(const char *version_str, u8 *major, u8 *minor, u8 *patch) {
    if (!version_str) return false;

    // Manual parsing without sscanf
    int maj = 0, min = 0, pat = 0;
    const char *p = version_str;

    // Parse major
    while (*p >= '0' && *p <= '9') {
        maj = maj * 10 + (*p - '0');
        p++;
    }
    if (*p != '.') return false;
    p++;

    // Parse minor
    while (*p >= '0' && *p <= '9') {
        min = min * 10 + (*p - '0');
        p++;
    }

    // Parse patch if exists
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            pat = pat * 10 + (*p - '0');
            p++;
        }
    }

    *major = (u8)maj;
    *minor = (u8)min;
    *patch = (u8)pat;
    return true;
}

// Helper to write debug log
void debug_log(const char *msg) {
    // Debug logging disabled - fuse-check-debug.txt will not be created
    (void)msg;
}

// Detect firmware from SystemVersion NCA in SYSTEM partition
// Requires BIS key 2 to be derived and set in SE
bool detect_firmware_from_nca(u8 *major, u8 *minor, u8 *patch, key_storage_t *keys) {
    bool result = false;

    debug_log("NCA: Start");

    // Try loading database (once) before scanning
    load_database();
    bool use_external_db = nca_db_count > 0;
    debug_log(use_external_db ? "NCA: Using database" : "NCA: No database loaded");

    // Initialize GPT list properly
    LIST_INIT(gpt);

    // Check if we have BIS key 2 (required for SYSTEM partition)
    if (!key_exists(keys->bis_key[2])) {
        debug_log("NCA: No BIS key 2");
        return false;
    }
    debug_log("NCA: BIS key 2 exists");

    // Set BIS key 2 in the security engine for SYSTEM partition
    se_aes_key_set(KS_BIS_02_CRYPT, keys->bis_key[2] + 0x00, SE_KEY_128_SIZE);
    se_aes_key_set(KS_BIS_02_TWEAK, keys->bis_key[2] + 0x10, SE_KEY_128_SIZE);
    debug_log("NCA: BIS keys set in SE");

    // Set eMMC to GPP partition
    if (!emummc_storage_set_mmc_partition(EMMC_GPP)) {
        debug_log("NCA: Failed to set GPP partition");
        return false;
    }
    debug_log("NCA: GPP partition set");

    // Parse GPT
    nx_emmc_gpt_parse(&gpt, &emmc_storage);
    debug_log("NCA: GPT parsed");

    // Find SYSTEM partition
    emmc_part_t *system_part = nx_emmc_part_find(&gpt, "SYSTEM");
    if (!system_part) {
        debug_log("NCA: SYSTEM partition not found");
        nx_emmc_gpt_free(&gpt);
        return false;
    }
    debug_log("NCA: SYSTEM partition found");

    // Initialize BIS for SYSTEM partition
    debug_log("NCA: About to call nx_emmc_bis_init");
    nx_emmc_bis_init(system_part);
    debug_log("NCA: nx_emmc_bis_init done");

    // Mount SYSTEM partition
    debug_log("NCA: About to mount SYSTEM");
    if (f_mount(&emmc_fs, "bis:", 1) != FR_OK) {
        debug_log("NCA: Mount failed");
        f_mount(NULL, "bis:", 1);
        nx_emmc_gpt_free(&gpt);
        return false;
    }
    debug_log("NCA: SYSTEM mounted");

    // Search for NCA files in /Contents/registered/
    DIR dir;
    FILINFO fno;
    debug_log("NCA: About to open directory");
    if (f_opendir(&dir, "bis:/Contents/registered") == FR_OK) {
        debug_log("NCA: Directory opened, scanning...");
        int file_count = 0;
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
            file_count++;
            if (use_external_db) {
                for (size_t i = 0; i < nca_db_count; i++) {
                    if (strcmp(fno.fname, nca_db[i].nca_filename) == 0) {
                        debug_log("NCA: Found match!");
                        if (parse_version_string(nca_db[i].version, major, minor, patch)) {
                            result = true;
                            break;
                        }
                    }
                }
            }
            if (result) break;
        }
        char buf[64];
        s_printf(buf, "NCA: Scanned %d files", file_count);
        debug_log(buf);
        f_closedir(&dir);
        debug_log("NCA: Directory closed");
    } else {
        debug_log("NCA: Failed to open directory");
    }

    // Unmount and cleanup
    debug_log("NCA: Unmounting");
    f_mount(NULL, "bis:", 1);
    nx_emmc_gpt_free(&gpt);
    debug_log("NCA: Cleanup done");

    return result;
}

void print_centered(int y, const char *text) {
    int len = strlen(text);
    int x = (1280 - (len * 16)) / 2; // Auto-calculate center
    gfx_con_setpos(x, y);
    gfx_puts(text);
}

void show_fuse_check_horizontal(u8 burnt_fuses, u8 fw_major, u8 fw_minor, u8 fw_patch, u8 required_fuses) {
    gfx_clear_grey(0x1B);

    // Title
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    print_centered(40, "NINTENDO SWITCH FUSE CHECKER 1.0.0");

    // System Information - single line
    gfx_con_setpos(200, 150);
    SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
    gfx_printf("Firmware: ");
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    gfx_printf("%2d.%d.%d", fw_major, fw_minor, fw_patch);

    gfx_con_setpos(200, 200);
    SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
    gfx_printf("Burnt Fuses: ");
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    gfx_printf("%2d", burnt_fuses);

    gfx_con_setpos(200, 250);
    SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
    gfx_printf("Required Fuses: ");
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    gfx_printf("%2d", required_fuses);

    // Status - large and clear
    gfx_con_setpos(200, 350);
    if (burnt_fuses < required_fuses) {
        SETCOLOR(COLOR_RED, COLOR_DEFAULT);
        gfx_puts("STATUS: FUSE MISMATCH");

        gfx_con_setpos(200, 400);
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_printf("Missing %d fuse(s) - OFW WILL NOT BOOT!", required_fuses - burnt_fuses);

        gfx_con_setpos(200, 450);
        gfx_puts("System will black screen on OFW boot");

        gfx_con_setpos(200, 520);
        SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
        gfx_puts("What will work: CFW (Atmosphere), Semi-stock (Hekate nogc)");
    } else if (burnt_fuses > required_fuses) {
        SETCOLOR(COLOR_RED, COLOR_DEFAULT);
        gfx_puts("STATUS: FUSE MISMATCH (OVERBURNT)");

        gfx_con_setpos(200, 400);
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_printf("Extra %d fuse(s) burnt - OFW WILL NOT BOOT!", burnt_fuses - required_fuses);

        gfx_con_setpos(200, 450);
        gfx_puts("System will black screen on OFW boot");

        gfx_con_setpos(200, 520);
        SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
        gfx_puts("What will work: CFW (Atmosphere), Semi-stock (Hekate nogc)");

        gfx_con_setpos(200, 570);
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_printf("Cannot downgrade below FW %d.x.x", burnt_fuses);
    } else {
        SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
        gfx_puts("STATUS: PERFECT MATCH");

        gfx_con_setpos(200, 400);
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_puts("Exact fuse count match - OFW WILL BOOT NORMALLY");

        gfx_con_setpos(200, 450);
        gfx_puts("All systems operational");
    }

    // Footer
    // X controls vertical position (top to bottom in landscape view)
    // Y controls horizontal position (left to right in landscape view)
    // Adjust Y value: smaller = more left, larger = more right
    // Screen width in landscape is 1280, so center is around 640
    SETCOLOR(COLOR_RED, COLOR_DEFAULT);
    print_centered(650, "VOL+:Fuse Map | VOL-:Back to Hekate | Power:Shutdown | 3-Finger:Screenshot");
}


static void show_fuse_info_page(int scroll_offset) {
    // Calculate how many entries fit on screen (from y=200 to y=620, footer at 650)
    const int entries_per_page = 15; // (620-200) / 28 rows spacing

    gfx_clear_grey(0x1B);
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    print_centered(80, "SWITCHBREW FUSE MAP");

    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    gfx_con_setpos(120, 160);
    gfx_printf("System Version");
    gfx_con_setpos(620, 160);
    gfx_printf("Prod Fuses");
    gfx_con_setpos(860, 160);
    gfx_printf("Dev Fuses");

    // Try loading database (once)
    load_database();

    int row_y = 200;
    if (fuse_db_count > 0) {

        // Display database entries with scrolling
        size_t start_idx = scroll_offset;
        size_t end_idx = start_idx + entries_per_page;
        if (end_idx > fuse_db_count) end_idx = fuse_db_count;

        for (size_t i = start_idx; i < end_idx; i++, row_y += 28) {
            SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
            gfx_con_setpos(120, row_y);
            gfx_printf("%s", fuse_db[i].version_range);

            gfx_con_setpos(640, row_y);
            SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
            gfx_printf("%2d", fuse_db[i].prod_fuses);

            gfx_con_setpos(880, row_y);
            SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
            gfx_printf("%2d", fuse_db[i].dev_fuses);
        }

        // Show scroll indicator if there are more entries
        if (fuse_db_count > entries_per_page) {
            gfx_con_setpos(1000, 620);
            SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
            gfx_printf("[%d-%d/%d]", (int)start_idx + 1, (int)end_idx, (int)fuse_db_count);
        }
    } else {
        // No database loaded
        gfx_con_setpos(120, row_y);
        SETCOLOR(COLOR_RED, COLOR_DEFAULT);
        gfx_printf("Database file not found!");
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_con_setpos(120, row_y + 40);
        gfx_printf("Please copy fusecheck_db.txt to:");
        gfx_con_setpos(120, row_y + 70);
        gfx_printf("sd:/config/fusecheck/fusecheck_db.txt");
    }

    SETCOLOR(COLOR_RED, COLOR_DEFAULT);
    print_centered(650, "VOL+:Scroll Down | VOL-:Scroll Up | Power:Back | 3-Finger:Screenshot");
}



void ipl_main() {
    // Initialize hardware
    hw_init();
    pivot_stack(IPL_STACK_TOP);
    heap_init(IPL_HEAP_START);
    set_default_configuration();

    // Initialize display
    display_init();
    u32 *fb = display_init_framebuffer_pitch();
    gfx_init_ctxt(fb, 720, 1280, 720);  // Portrait mode, software rotation in gfx_con_setpos
    gfx_con_init();
    display_backlight_pwm_init();
    display_backlight_brightness(100, 1000);

    // Mount SD Card
    h_cfg.errors |= !sd_mount() ? ERR_SD_BOOT_EN : 0;

    // Train DRAM
    if (minerva_init())
        h_cfg.errors |= ERR_LIBSYS_MTC;

    // Overclock BPMP
    bpmp_clk_rate_set(h_cfg.t210b01 ? BPMP_CLK_DEFAULT_BOOST : BPMP_CLK_LOWER_BOOST);
    minerva_change_freq(FREQ_800);

    // Load emuMMC config
    emummc_load_cfg();
    h_cfg.emummc_force_disable = 1;  // Force sysMMC for fuse check

    // Derive keys silently in RAM (no file saving, suppress errors)
    key_storage_t __attribute__((aligned(4))) keys = {0};
    gfx_con.mute = true;  // Mute gfx output to suppress warnings
    bool keys_derived = derive_bis_keys_silently(&keys);
    gfx_con.mute = false;

    if (!keys_derived) {
        gfx_clear_grey(0x1B);
        gfx_con_setpos(0, 0);
        SETCOLOR(COLOR_RED, COLOR_DEFAULT);
        gfx_printf("\nFailed to derive keys!\n");
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        btn_wait();
        power_set_state(POWER_OFF_REBOOT);
        while (true) bpmp_halt();
    }

    // Get burnt fuses
    u8 burnt_fuses = get_burnt_fuses();

    // Detect firmware version from NCA
    u8 fw_major = 0, fw_minor = 0, fw_patch = 0;
    bool fw_detected = false;

    if (emummc_storage_init_mmc() == 0) {
        // Try NCA detection
        fw_detected = detect_firmware_from_nca(&fw_major, &fw_minor, &fw_patch, &keys);
        emummc_storage_end();
    }

    if (!fw_detected) {
        // Default to a safe version if detection completely fails
        fw_major = 1;
        fw_minor = 0;
        fw_patch = 0;
    }

    // Calculate required fuses
    u8 required_fuses = get_required_fuses(fw_major, fw_minor);

    // Initialize touchscreen for 3-finger screenshot support
    touch_power_on();

    // Show results in horizontal layout (single page)
    show_fuse_check_horizontal(burnt_fuses, fw_major, fw_minor, fw_patch, required_fuses);

    // Wait for button to exit, support info page, scrolling, and screenshot combo
    bool on_info_page = false;
    int scroll_offset = 0;
    const int entries_per_page = 15;

    u32 btn_last = btn_read();

    while (true)
    {
        // Poll for touch events (non-blocking)
        touch_event touch = {0};
        touch_poll(&touch);

        // Check for 3-finger touch screenshot
        if (touch.touch && touch.fingers >= 3)
        {
            // Wait for touch release to avoid multiple screenshots
            msleep(100);

            if (!save_fb_to_bmp())
            {
                SETCOLOR(COLOR_GREEN, COLOR_DEFAULT);
                print_centered(620, "Screenshot saved!");
                msleep(1000);
            }
            else
            {
                SETCOLOR(COLOR_RED, COLOR_DEFAULT);
                print_centered(620, "Screenshot failed!");
                msleep(1000);
            }
            if (on_info_page)
                show_fuse_info_page(scroll_offset);
            else
                show_fuse_check_horizontal(burnt_fuses, fw_major, fw_minor, fw_patch, required_fuses);

            btn_last = btn_read();
            continue;
        }

        // Non-blocking button read
        u32 btn = btn_read();

        // Only process button presses (ignore button releases and repeats)
        if (btn == btn_last)
        {
            msleep(10);
            continue;
        }

        btn_last = btn;

        // Ignore button releases (when btn becomes 0)
        if (!btn)
        {
            msleep(10);
            continue;
        }

        bool vol_up = btn & BTN_VOL_UP;
        bool vol_dn = btn & BTN_VOL_DOWN;

        // On main page: VOL+ toggles to info page, VOL- goes back to Hekate
        if (!on_info_page)
        {
            if (vol_up)
            {
                on_info_page = true;
                scroll_offset = 0; // Reset scroll when entering info page
                show_fuse_info_page(scroll_offset);
                continue;
            }

            if (vol_dn)
                break; // Back to Hekate
        }
        // On info page: VOL+ scrolls down, VOL- scrolls up, Power goes back to main
        else
        {
            if (vol_up)
            {
                // Scroll down (stop at bottom, don't change pages)
                load_database(); // Ensure database is loaded to get count
                int max_scroll = (int)fuse_db_count - entries_per_page;
                if (max_scroll < 0) max_scroll = 0;

                if (scroll_offset < max_scroll)
                {
                    scroll_offset++;
                    show_fuse_info_page(scroll_offset);
                }
                // If at bottom, do nothing (just stay there)
                continue;
            }

            if (vol_dn)
            {
                // Scroll up (stop at top, don't change pages)
                if (scroll_offset > 0)
                {
                    scroll_offset--;
                    show_fuse_info_page(scroll_offset);
                }
                // If at top, do nothing (just stay there)
                continue;
            }

            // Power: go back to main page when on fuse list
            if (btn & BTN_POWER)
            {
                on_info_page = false;
                show_fuse_check_horizontal(burnt_fuses, fw_major, fw_minor, fw_patch, required_fuses);
                continue;
            }
        }

        // Power on main page: shutdown
        if (!on_info_page && (btn & BTN_POWER))
        {
            power_set_state(POWER_OFF);
            break;
        }
    }

    // Launch bootloader/update.bin instead of reboot
    FILINFO fno;
    if (!f_stat("sd:/bootloader/update.bin", &fno)) {
        // Launch update.bin
        payload_path = "sd:/bootloader/update.bin";
    } else if (!f_stat("sd:/payload.bin", &fno)) {
        // Fallback to payload.bin
        payload_path = "sd:/payload.bin";
    } else {
        // No payload found, just reboot
        power_set_state(POWER_OFF_REBOOT);
        while (true) bpmp_halt();
    }

    // Launch payload
    launch_payload(payload_path);

    // If launch fails, reboot
    power_set_state(POWER_OFF_REBOOT);
    while (true)
        bpmp_halt();
}
