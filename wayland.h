/*
 * vim:ts=4:sw=4:expandtab
 */
#ifndef WAYLAND_H
#define WAYLAND_H

#include <wayland-client.h>
#include <cairo.h>

struct input;

struct display {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shell *shell;
    struct wl_shm *shm;
    int display_fd;
    uint32_t formats;
    struct input *input;
    struct xkb_context *xkb_context;
    void (*key_handler)(struct input *input, uint32_t time, uint32_t key, uint32_t unicode, enum wl_keyboard_key_state state);
};

struct input {
    struct display *display;
    struct wl_keyboard *keyboard;
    struct wl_seat *seat;

    struct {
        struct xkb_keymap *keymap;
        struct xkb_state *state;
    } xkb;
};

struct window {
    struct display *display;
    int width, height;
    struct wl_surface *surface;
    struct wl_shell_surface *shell_surface;
    void (*redraw_handler)(struct window *window, cairo_t *cairo_context);
};

struct display *create_display(void);
void destroy_display(struct display *display);
void display_run(struct display *display);

struct window *create_window(struct display *display, int width, int height);
void destroy_window(struct window *window);
void window_schedule_redraw(struct window *window);
void window_await_frame(struct window *window);

#endif
