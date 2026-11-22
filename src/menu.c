/**
 * @file menu.c
 * @brief Minimal Fortnite framebuffer menu.
 *
 * Input:
 *   R1 + Square  - open/close
 *   D-pad Up/Down - selection
 *   Cross        - activate
 *   Circle       - close
 *
 * The first version intentionally contains no game commands. It is a
 * rendering/input proof-of-concept for the existing VideoOut overlay path.
 */

#include "menu.h"
#include "overlay.h"
#include "plugin_common.h"

#include <string.h>
#include <orbis/Pad.h>

#define MENU_COUNT       4
#define MENU_W           700
#define MENU_H           420
#define MENU_ITEM_H      58
#define MENU_ITEM_GAP    10

static bool g_visible = false;
static int  g_selected = 0;
static uint32_t g_previous_buttons = 0;

static const char *g_items[MENU_COUNT] = {
    "Test Overlay",
    "Settings",
    "Fortnite Menu",
    "Close Menu"
};

static bool just_pressed(uint32_t buttons, uint32_t mask)
{
    return (buttons & mask) && !(g_previous_buttons & mask);
}

void menu_init(void)
{
    g_visible = false;
    g_selected = 0;
    g_previous_buttons = 0;
}

bool menu_is_visible(void)
{
    return g_visible;
}

void menu_poll(uint32_t buttons)
{
    const uint32_t toggle = PAD_BUTTON_R1 | PAD_BUTTON_SQUARE;

    if ((buttons & toggle) == toggle &&
        (g_previous_buttons & toggle) != toggle) {
        g_visible = !g_visible;
        LOG_INFO("FN menu %s", g_visible ? "OPEN" : "CLOSED");
    }

    if (g_visible) {
        if (just_pressed(buttons, PAD_BUTTON_UP)) {
            g_selected = (g_selected + MENU_COUNT - 1) % MENU_COUNT;
        } else if (just_pressed(buttons, PAD_BUTTON_DOWN)) {
            g_selected = (g_selected + 1) % MENU_COUNT;
        }

        if (just_pressed(buttons, PAD_BUTTON_CROSS)) {
            LOG_INFO("FN menu selection=%d (%s)", g_selected, g_items[g_selected]);

            if (g_selected == 3) {
                g_visible = false;
            }
        }

        if (just_pressed(buttons, PAD_BUTTON_CIRCLE)) {
            g_visible = false;
        }
    }

    g_previous_buttons = buttons;
}

void menu_draw(uint32_t *fb, uint32_t pitch,
               uint32_t width, uint32_t height)
{
    if (!g_visible || !fb || width == 0 || height == 0)
        return;

    const int x = ((int)width - MENU_W) / 2;
    const int y = ((int)height - MENU_H) / 2;

    const uint32_t bg       = OVERLAY_COLOR(14, 16, 21);
    const uint32_t border   = OVERLAY_COLOR(70, 75, 88);
    const uint32_t item_bg  = OVERLAY_COLOR(28, 31, 39);
    const uint32_t selected = OVERLAY_COLOR(35, 105, 190);
    const uint32_t white    = OVERLAY_COLOR(255, 255, 255);
    const uint32_t dim      = OVERLAY_COLOR(170, 175, 185);

    /* Outer panel. */
    overlay_draw_rect(fb, pitch, x, y, MENU_W, MENU_H, border);
    overlay_draw_rect(fb, pitch, x + 2, y + 2, MENU_W - 4, MENU_H - 4, bg);

    overlay_draw_text_2x(fb, pitch, x + 28, y + 24,
                         "ENViXIFY MENU", white, bg);

    for (int i = 0; i < MENU_COUNT; i++) {
        int iy = y + 72 + i * (MENU_ITEM_H + MENU_ITEM_GAP);
        uint32_t color = (i == g_selected) ? selected : item_bg;

        overlay_draw_rect(fb, pitch, x + 24, iy,
                          MENU_W - 48, MENU_ITEM_H, color);

        overlay_draw_text_2x(fb, pitch, x + 42, iy + 18,
                             g_items[i], white, color);
    }

    overlay_draw_text(fb, pitch, x + 28, y + MENU_H - 26,
                      "R1+[] OPEN/CLOSE   D-PAD MOVE   X SELECT   O CLOSE",
                      dim, bg);
}
