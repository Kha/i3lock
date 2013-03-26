#ifndef WAYLAND_H
#define WAYLAND_H

#include <wayland-client.h>

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_shm *shm;
	uint32_t formats;
};

struct buffer {
	struct wl_buffer *buffer;
	void *shm_data;
	int busy;
};

struct window {
	struct display *display;
	int width, height;
	struct wl_surface *surface;
	struct wl_shell_surface *shell_surface;
	struct buffer buffers[2];
	struct buffer *prev_buffer;
	struct wl_callback *callback;
};

struct display *create_display(void);
void destroy_display(struct display *display);

struct window *create_window(struct display *display, int width, int height);
void destroy_window(struct window *window);

void draw(struct window *window);

#endif
