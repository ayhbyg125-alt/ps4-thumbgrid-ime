/**
 * @file main.c
 * @brief GoldHEN plugin entry point for ThumbGrid IME
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "plugin_common.h"
#include "ime_hook.h"
#include "overlay.h"
#include "thumbgrid_ipc.h"
#include "input.h"

#include <GoldHEN.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/_types/kernel.h>
#include <unistd.h>

/* ─── Forward Declarations ────────────────────────────────────────── */

static int32_t load_required_modules(void);

/* ─── Visual hider controller ───────────────────────────────────── */
static volatile bool g_hider_running = false;
static volatile bool g_hider_active = true;
static OrbisPthread g_hider_thread;
static int32_t g_hider_pad = -1;
static bool g_hider_pad_owned = false;

#define HIDER_X       0
#define HIDER_Y       0
#define HIDER_WIDTH   1920
#define HIDER_HEIGHT  1080

static void hider_publish(void) {
    /* The shell overlay reads the same IPC file used by ThumbGrid. */
    int fd = sceKernelOpen(TG_IPC_PATH, 0x0202 /* O_RDWR | O_CREAT */, 0666);
    if (fd < 0) {
        LOG_ERROR("Hider IPC open failed: %s rc=0x%08X", TG_IPC_PATH, fd);
        return;
    }

    /* Ensure the shared file is large enough even when IME has never opened it. */
    if (sceKernelLseek(fd, TG_IPC_FILE_SIZE - 1, 0 /* SEEK_SET */) < 0) {
        LOG_ERROR("Hider IPC resize seek failed");
        sceKernelClose(fd);
        return;
    }
    char zero = 0;
    if (sceKernelWrite(fd, &zero, 1) != 1) {
        LOG_ERROR("Hider IPC resize write failed");
        sceKernelClose(fd);
        return;
    }
    sceKernelLseek(fd, 0, 0 /* SEEK_SET */);

    void *addr = NULL;
    int rc = sceKernelMmap(0, TG_IPC_FILE_SIZE, 0x03, 0x0001, fd, 0, &addr);
    if (rc < 0 || !addr || addr == (void *)-1) {
        LOG_ERROR("Hider IPC mmap failed rc=0x%08X", rc);
        sceKernelClose(fd);
        return;
    }

    ThumbGridSharedState *s = (ThumbGridSharedState *)addr;
    thumbgrid_ipc_write_begin(s);
    s->hider_active = g_hider_active ? 1u : 0u;
    s->hider_x = HIDER_X;
    s->hider_y = HIDER_Y;
    s->hider_width = HIDER_WIDTH;
    s->hider_height = HIDER_HEIGHT;
    thumbgrid_ipc_write_end(s);
    LOG_INFO("Hider IPC published active=%u path=%s", s->hider_active, TG_IPC_PATH);

    sceKernelMunmap(addr, TG_IPC_FILE_SIZE);
    sceKernelClose(fd);
}

static void *hider_thread_main(void *arg) {
    (void)arg;

    int32_t uid = 0;
    if (sceUserServiceGetInitialUser(&uid) < 0) uid = 0;

    g_hider_pad = scePadGetHandle(uid, 0, 0);
    if (g_hider_pad < 0) {
        g_hider_pad = scePadOpen(uid, 0, 0, NULL);
        if (g_hider_pad >= 0) g_hider_pad_owned = true;
    }

    /* Do not expose the hider outside Fortnite. */
    OrbisAppInfo app_info;
    memset(&app_info, 0, sizeof(app_info));
    bool fortnite = false;
    if (sceKernelGetAppInfo(getpid(), &app_info) == 0) {
        fortnite = (strncmp(app_info.TitleId, "CUSA07669", sizeof(app_info.TitleId)) == 0);
        LOG_INFO("Hider title gate: %.10s (%s)", app_info.TitleId,
                 fortnite ? "Fortnite" : "not Fortnite");
    } else {
        LOG_WARN("Hider title gate: sceKernelGetAppInfo failed");
    }

    uint32_t previous = 0;
    /* CUSA07669-specific fallback: draw directly into Fortnite's VideoOut
     * framebuffer. This does not depend on the SceShellUI PUI path. */
    overlay_set_hider_active(fortnite && g_hider_active);
    LOG_INFO("Hider framebuffer state: fortnite=%d active=%d overlay=%d",
             fortnite, g_hider_active, overlay_is_active());
    if (fortnite) {
        hider_publish();
        notify("FN Menu Hider: CUSA07669 READY");
    }

    while (g_hider_running) {
        OrbisPadData pad;
        memset(&pad, 0, sizeof(pad));

        if (g_hider_pad >= 0 && scePadReadState(g_hider_pad, &pad) == 0) {
            uint32_t combo = PAD_BUTTON_L1 | PAD_BUTTON_DOWN;
            if (fortnite &&
                (pad.buttons & combo) == combo &&
                (previous & combo) != combo) {
                g_hider_active = !g_hider_active;
                overlay_set_hider_active(g_hider_active);
                hider_publish();
                notify("FN Menu Hider: %s",
                       g_hider_active ? "ON" : "OFF");
            }
            previous = pad.buttons;
        }

        sceKernelUsleep(16000);
    }

    if (g_hider_pad >= 0 && g_hider_pad_owned) {
        scePadClose(g_hider_pad);
    }
    g_hider_pad = -1;
    g_hider_pad_owned = false;
    return NULL;
}

/* ─── On-Screen Notification ─────────────────────────────────────── */

void notify(const char *fmt, ...) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type     = NotificationRequest;
    req.reqId    = 0;
    req.priority = 0;
    req.msgId    = 0;
    req.targetId = -1;
    req.userId   = -1;

    va_list args;
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);

    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* ─── Plugin Entry ────────────────────────────────────────────────── */

int module_start(size_t argc, const void *args) {
    (void)argc;
    (void)args;

    LOG_INFO("=== %s v%d.%d.%d starting ===",
        PLUGIN_NAME,
        (PLUGIN_VER >> 16) & 0xFF,
        (PLUGIN_VER >> 8) & 0xFF,
        PLUGIN_VER & 0xFF);

    int32_t rc = load_required_modules();
    if (rc != IME_OK) {
        LOG_ERROR("Failed to load required modules (rc=%d)", rc);
        notify("%s: module load FAILED (%d)", PLUGIN_NAME, rc);
        return rc;
    }

    rc = ime_hook_install();
    if (rc != IME_OK) {
        LOG_ERROR("Failed to install IME hooks (rc=%d)", rc);
        notify("%s: hook install FAILED (%d)", PLUGIN_NAME, rc);
        return rc;
    }

    g_hider_running = true;
    int hider_thread_rc = scePthreadCreate(&g_hider_thread, NULL, hider_thread_main, NULL, "fn_hider");
    if (hider_thread_rc != 0) {
        g_hider_running = false;
        LOG_WARN("Hider thread create failed: 0x%08X", hider_thread_rc);
    }

    rc = overlay_init();
    if (rc != IME_OK) {
        LOG_WARN("Overlay init returned %d (non-fatal)", rc);
    }

    /* Load shell overlay into SceShellUI for PUI-based rendering.
     * Try multiple paths — kernel resolves paths outside app sandbox. */
    static const char *sovl_paths[] = {
        "/data/shell_overlay.prx",
        "/data/GoldHEN/plugins/shell_overlay.prx",
        "/user/data/shell_overlay.prx",
        "/user/data/GoldHEN/plugins/shell_overlay.prx",
        NULL
    };
    int sovl_rc = -1;
    for (int i = 0; sovl_paths[i]; i++) {
        LOG_INFO("Trying SOVL load: %s", sovl_paths[i]);
        sovl_rc = sys_sdk_proc_prx_load("SceShellUI",
                                         (char*)sovl_paths[i]);
        LOG_INFO("  -> rc=0x%08X (%d)", sovl_rc, sovl_rc);
        if (sovl_rc >= 0) break;
    }
    LOG_INFO("Plugin loaded successfully - IME hooks + overlay active");
    notify("%s v%d.%d.%d loaded", PLUGIN_NAME,
        (PLUGIN_VER >> 16) & 0xFF,
        (PLUGIN_VER >> 8) & 0xFF,
        PLUGIN_VER & 0xFF);
    return IME_OK;
}

int module_stop(size_t argc, const void *args) {
    (void)argc;
    (void)args;

    LOG_INFO("Plugin shutting down...");

    g_hider_running = false;
    if (g_hider_thread) {
        scePthreadJoin(g_hider_thread, NULL);
        g_hider_thread = NULL;
    }

    overlay_cleanup();

    int32_t rc = ime_hook_remove();
    if (rc != IME_OK) {
        LOG_WARN("Hook removal returned %d", rc);
    }

    LOG_INFO("=== %s stopped ===", PLUGIN_NAME);
    return IME_OK;
}

/* ─── Module Loading ──────────────────────────────────────────────── */

static int32_t load_required_modules(void) {
    int32_t rc;

    /* Load Common Dialog (internal system module) */
    rc = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_COMMON_DIALOG);
    if (rc < 0 && rc != (int32_t)0x80020133) {
        LOG_ERROR("Failed to load CommonDialog: 0x%08X", rc);
        return IME_ERROR_MODULE_LOAD;
    }
    LOG_DEBUG("CommonDialog: %s",
        rc == (int32_t)0x80020133 ? "already loaded" : "loaded");

    /* Load IME Dialog module */
    rc = sceSysmoduleLoadModule(SCE_SYSMODULE_IME_DIALOG);
    if (rc < 0 && rc != (int32_t)0x80020133) {
        LOG_ERROR("Failed to load ImeDialog: 0x%08X", rc);
        return IME_ERROR_MODULE_LOAD;
    }
    LOG_DEBUG("ImeDialog: %s",
        rc == (int32_t)0x80020133 ? "already loaded" : "loaded");

    /* Load Pad module (likely already loaded by game) */
    rc = sceSysmoduleLoadModule(SCE_SYSMODULE_PAD);
    if (rc < 0 && rc != (int32_t)0x80020133) {
        LOG_WARN("Failed to load Pad module: 0x%08X (non-fatal)", rc);
    } else {
        LOG_DEBUG("Pad: %s",
            rc == (int32_t)0x80020133 ? "already loaded" : "loaded");
    }

    /* Load UserService module (likely already loaded by game) */
    rc = sceSysmoduleLoadModule(SCE_SYSMODULE_USER_SERVICE);
    if (rc < 0 && rc != (int32_t)0x80020133) {
        LOG_WARN("Failed to load UserService: 0x%08X (non-fatal)", rc);
    } else {
        LOG_DEBUG("UserService: %s",
            rc == (int32_t)0x80020133 ? "already loaded" : "loaded");
    }

    /* Load VideoOut module (likely already loaded by game) */
    rc = sceSysmoduleLoadModule(SCE_SYSMODULE_VIDEO_OUT);
    if (rc < 0 && rc != (int32_t)0x80020133) {
        LOG_WARN("Failed to load VideoOut: 0x%08X (non-fatal)", rc);
    } else {
        LOG_DEBUG("VideoOut: %s",
            rc == (int32_t)0x80020133 ? "already loaded" : "loaded");
    }

    /* Initialize pad library (safe to call if already initialized) */
    rc = scePadInit();
    if (rc < 0 && rc != (int32_t)0x80920002) {
        LOG_WARN("scePadInit: 0x%08X (non-fatal)", rc);
    } else {
        LOG_DEBUG("scePadInit: %s",
            rc == (int32_t)0x80920002 ? "already initialized" : "OK");
    }

    return IME_OK;
}
