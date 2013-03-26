#ifndef WAYLAND_H
#define WAYLAND_H

#include <wayland-client.h>
#include <cairo.h>

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_shm *shm;
	uint32_t formats;
};

struct window {
	struct display *display;
	int width, height;
	struct wl_surface *surface;
	struct wl_shell_surface *shell_surface;
};

struct display *create_display(void);
void destroy_display(struct display *display);

struct window *create_window(struct display *display, int width, int height);
void destroy_window(struct window *window);
cairo_t *window_acquire_cairo_context(struct window *window);
void window_release_cairo_context(struct window *window, cairo_t *cairo);

#endif
