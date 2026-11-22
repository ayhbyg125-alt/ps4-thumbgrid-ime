/**
 * @file menu.h
 * @brief Minimal Fortnite framebuffer menu.
 *
 * This menu deliberately uses the existing VideoOut framebuffer overlay
 * already present in the plugin. It does not depend on SceShellUI, Mono,
 * Fortnite Slate objects, or game-specific UI addresses.
 */

#ifndef FN_MENU_H
#define FN_MENU_H

#include <stdint.h>
#include <stdbool.h>

void menu_init(void);
void menu_poll(uint32_t buttons);
void menu_draw(uint32_t *fb, uint32_t pitch, uint32_t width, uint32_t height);
bool menu_is_visible(void);

#endif /* FN_MENU_H */
