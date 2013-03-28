/*
 * vim:ts=4:sw=4:expandtab
 *
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2010 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client.h>

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
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;

    struct {
        struct xkb_keymap *keymap;
        struct xkb_state *state;
    } xkb;
};

struct buffer {
    struct wl_buffer *buffer;
    cairo_surface_t *cairo_surface;
    void *shm_data;
    int shm_size;
    int busy;
};

struct window {
    struct display *display;
    int width, height;
    struct wl_surface *surface;
    struct wl_shell_surface *shell_surface;
    void (*redraw_handler)(struct window *window, cairo_t *cairo_context);

    struct buffer buffers[2];
    struct buffer *current;
    bool redrawing;
    bool redraw_scheduled;
};

static void buffer_release(void *data, struct wl_buffer *buffer) {
    struct buffer *mybuf = data;
    mybuf->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
    buffer_release
};

static int os_create_anonymous_file(off_t size) {
    static const char template[] = "/i3lock-shared-XXXXXX";
    const char *path;
    char *name;
    int fd;

    path = getenv("XDG_RUNTIME_DIR");
    if (!path)
        return -1;

    name = malloc(strlen(path) + sizeof(template));
    if (!name)
        return -1;

    strcpy(name, path);
    strcat(name, template);

    fd = mkstemp(name);
    if (fd >= 0)
        unlink(name);

    free(name);

    if (fd < 0)
        return -1;

    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int buffer_init(struct buffer *buffer, struct display *display, int width, int height) {
    struct wl_shm_pool *pool;
    int fd, size, stride;
    void *data;

    stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    size = stride * height;

    fd = os_create_anonymous_file(size);
    if (fd < 0) {
        fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
            size);
        return -1;
    }

    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %m\n");
        close(fd);
        return -1;
    }

    buffer->cairo_surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, width, height, stride);

    pool = wl_shm_create_pool(display->shm, fd, size);
    buffer->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
    wl_shm_pool_destroy(pool);
    close(fd);

    buffer->shm_data = data;
    buffer->shm_size = size;

    return 0;
}

static void buffer_reset(struct buffer *buffer) {
    cairo_surface_destroy(buffer->cairo_surface);
    wl_buffer_destroy(buffer->buffer);
    munmap(buffer->shm_data, buffer->shm_size);
}

static void window_redraw(struct window *window);

static void frame_callback(void *data, struct wl_callback *callback, uint32_t time) {
    struct window *window = data;

    wl_callback_destroy(callback);
    window->redrawing = false;
    if (window->redraw_scheduled) {
        window->redraw_scheduled = false;
        window_redraw(window);
    }
}

static const struct wl_callback_listener listener = {
    frame_callback
};

static void window_redraw(struct window *window) {
    if (!window->redraw_handler)
        return;

    window->redrawing = true;
    struct buffer *buffer;

    /* pick a free buffer from the two */
    if (!window->buffers[0].busy)
        buffer = &window->buffers[0];
    else
        buffer = &window->buffers[1];

    if (!buffer->cairo_surface ||
        cairo_image_surface_get_width(buffer->cairo_surface) != window->width ||
        cairo_image_surface_get_height(buffer->cairo_surface) != window->height) {

        if (buffer->cairo_surface)
            buffer_reset(buffer);

        buffer_init(buffer, window->display, window->width, window->height);
    }

    if (window->current != buffer)
        wl_surface_attach(window->surface, buffer->buffer, 0, 0);
    window->current = buffer;

    cairo_t *cairo = cairo_create(buffer->cairo_surface);
    window->redraw_handler(window, cairo);
    cairo_destroy(cairo);

    window->current->busy = 1;
    wl_callback_add_listener(wl_surface_frame(window->surface), &listener, window);
    wl_surface_damage(window->surface, 0, 0, window->width, window->height);
    wl_surface_commit(window->surface);
}

void window_schedule_redraw(struct window *window) {
    if (!window->redrawing)
        window_redraw(window);
    else
        window->redraw_scheduled = true;
}


static void handle_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial) {
    wl_shell_surface_pong(shell_surface, serial);
}

static void handle_configure(void *data, struct wl_shell_surface *shell_surface, uint32_t edges, int32_t width, int32_t height) {
    struct window *window = data;
    window->width = width;
    window->height = height;
    window_schedule_redraw(window);
}

static void handle_popup_done(void *data, struct wl_shell_surface *shell_surface) {
}

static const struct wl_shell_surface_listener shell_surface_listener = {
    handle_ping,
    handle_configure,
    handle_popup_done
};

struct window *create_window(struct display *display, int width, int height) {
    struct window *window;

    window = calloc(1, sizeof *window);
    if (!window)
        return NULL;

    window->display = display;
    window->width = width;
    window->height = height;
    window->surface = wl_compositor_create_surface(display->compositor);
    window->shell_surface = wl_shell_get_shell_surface(display->shell, window->surface);

    if (window->shell_surface)
        wl_shell_surface_add_listener(window->shell_surface, &shell_surface_listener, window);

    wl_shell_surface_set_title(window->shell_surface, "i3lock");

    wl_shell_surface_set_toplevel(window->shell_surface);

    return window;
}

void destroy_window(struct window *window) {
    if (window->buffers[0].buffer)
        wl_buffer_destroy(window->buffers[0].buffer);
    if (window->buffers[1].buffer)
        wl_buffer_destroy(window->buffers[1].buffer);

    wl_shell_surface_destroy(window->shell_surface);
    wl_surface_destroy(window->surface);
    free(window);
}

static void shm_format(void *data, struct wl_shm *wl_shm, uint32_t format) {
    struct display *d = data;

    d->formats |= (1 << format);
}

struct wl_shm_listener shm_listenter = {
    shm_format
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size) {
    struct input *input = data;
    char *map_str;

    if (!data) {
        close(fd);
        return;
    }

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    input->xkb.keymap = xkb_map_new_from_string(input->display->xkb_context,
                            map_str,
                            XKB_KEYMAP_FORMAT_TEXT_V1,
                            0);
    munmap(map_str, size);
    close(fd);

    if (!input->xkb.keymap) {
        fprintf(stderr, "failed to compile keymap\n");
        return;
    }

    input->xkb.state = xkb_state_new(input->xkb.keymap);
    if (!input->xkb.state) {
        fprintf(stderr, "failed to create XKB state\n");
        xkb_map_unref(input->xkb.keymap);
        input->xkb.keymap = NULL;
        return;
    }
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface,
              struct wl_array *keys) {
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key,
            uint32_t state_w)
{
    struct input *input = data;
    uint32_t code, num_syms;
    enum wl_keyboard_key_state state = state_w;
    const xkb_keysym_t *syms;
    xkb_keysym_t sym;

    code = key + 8;
    if (!input->xkb.state)
        return;

    num_syms = xkb_key_get_syms(input->xkb.state, code, &syms);

    sym = XKB_KEY_NoSymbol;
    if (num_syms == 1)
        sym = syms[0];

    if (input->display->key_handler) {
        (*input->display->key_handler)(input, time, key,
                       sym, state);
    }
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed,
              uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    struct input *input = data;

    xkb_state_update_mask(input->xkb.state, mods_depressed, mods_latched,
                  mods_locked, 0, 0, group);
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_handle_keymap,
    keyboard_handle_enter,
    keyboard_handle_leave,
    keyboard_handle_key,
    keyboard_handle_modifiers,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat, enum wl_seat_capability caps) {
    struct input *input = data;

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !input->keyboard) {
        input->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_set_user_data(input->keyboard, input);
        wl_keyboard_add_listener(input->keyboard, &keyboard_listener,
                     input);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && input->keyboard) {
        wl_keyboard_destroy(input->keyboard);
        input->keyboard = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
};

static void display_add_input(struct display *d, uint32_t id) {
    struct input *input = d->input;

    input->seat = wl_registry_bind(d->registry, id, &wl_seat_interface, 1);

    wl_seat_add_listener(input->seat, &seat_listener, input);
    wl_seat_set_user_data(input->seat, input);
}

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    struct display *d = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        d->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_shell") == 0) {
        d->shell = wl_registry_bind(registry, id, &wl_shell_interface, 1);
    } else if (strcmp(interface, "wl_shm") == 0) {
        d->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
        wl_shm_add_listener(d->shm, &shm_listenter, d);
    } else if (strcmp(interface, "wl_seat") == 0) {
        display_add_input(d, id);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

struct display *create_display(void) {
    struct display *display;

    display = malloc(sizeof *display);
    display->display = wl_display_connect(NULL);
    if (display->display == NULL)
        return NULL;

    display->xkb_context = xkb_context_new(0);
    if (display->xkb_context == NULL) {
        fprintf(stderr, "Failed to create XKB context\n");
        return NULL;
    }

    display->formats = 0;
    display->input = calloc(sizeof(struct input), 1);
    display->input->display = display;
    display->registry = wl_display_get_registry(display->display);
    wl_registry_add_listener(display->registry, &registry_listener, display);
    wl_display_roundtrip(display->display);
    if (display->shm == NULL) {
        fprintf(stderr, "No wl_shm global\n");
        exit(1);
    }

    wl_display_roundtrip(display->display);

    if (!(display->formats & (1 << WL_SHM_FORMAT_ARGB8888))) {
        fprintf(stderr, "WL_SHM_FORMAT_ARGB32 not available\n");
        exit(1);
    }

    display->display_fd = wl_display_get_fd(display->display);

    return display;
}

void destroy_display(struct display *display) {
    if (display->shm)
        wl_shm_destroy(display->shm);

    if (display->shell)
        wl_shell_destroy(display->shell);

    if (display->compositor)
        wl_compositor_destroy(display->compositor);

    xkb_context_unref(display->xkb_context);
    wl_registry_destroy(display->registry);
    wl_display_flush(display->display);
    wl_display_disconnect(display->display);
    free(display);
}

void display_run(struct display *display) {
    int ret = 0;

    while (ret != -1)
        ret = wl_display_dispatch(display->display);
}
