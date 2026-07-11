/*
 * fakepointer - drive the KDE Wayland pointer via org_kde_kwin_fake_input.
 * Same mechanism KDE Connect uses for remote input; same-user, no root.
 * Used by the live-verification loop to hover dock items (task previews,
 * tooltips, parabolic zoom) and click UI without a human at the desk.
 *
 * usage: fakepointer move <x> <y>
 *        fakepointer click <x> <y>     (left click at absolute position)
 *
 * build (inside the devshell):
 *   xml=$(pkg-config --variable=pkgdatadir plasma-wayland-protocols)/fake-input.xml
 *   wayland-scanner client-header "$xml" fake-input-client-protocol.h
 *   wayland-scanner private-code  "$xml" fake-input-protocol.c
 *   cc -O2 -o fakepointer fakepointer.c fake-input-protocol.c \
 *      $(pkg-config --cflags --libs wayland-client)
 *
 * KWin filters restricted globals per client: without authorization the
 * registry never lists fake_input. Authorize by installing a desktop file
 * in kwin's XDG_DATA_DIRS (e.g. ~/.local/share/applications) whose Exec is
 * the absolute path of this binary and which declares
 *   X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input
 * then run kbuildsycoca6. Same gate family as the dock's own
 * window-management/screencast access (org.kde.latte-dock.desktop).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include "fake-input-client-protocol.h"

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111

static struct org_kde_kwin_fake_input *fake_input = NULL;

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version)
{
    (void)data;
    if (strcmp(interface, org_kde_kwin_fake_input_interface.name) == 0) {
        uint32_t v = version < 5 ? version : 5;
        fake_input = wl_registry_bind(registry, name, &org_kde_kwin_fake_input_interface, v);
        if (v < 5) {
            fprintf(stderr, "org_kde_kwin_fake_input version %u < 5, no absolute motion\n", v);
            exit(3);
        }
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int main(int argc, char **argv)
{
    if (argc != 4 || (strcmp(argv[1], "move") && strcmp(argv[1], "click") && strcmp(argv[1], "rightclick"))) {
        fprintf(stderr, "usage: %s move|click|rightclick <x> <y>\n", argv[0]);
        return 2;
    }
    double x = atof(argv[2]);
    double y = atof(argv[3]);

    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "cannot connect to wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!fake_input) {
        fprintf(stderr, "compositor does not expose org_kde_kwin_fake_input (desktop file missing?)\n");
        return 1;
    }

    org_kde_kwin_fake_input_authenticate(fake_input,
        "latte-dock-dev-verify", "drive hover interactions for dock verification");
    org_kde_kwin_fake_input_pointer_motion_absolute(fake_input,
        wl_fixed_from_double(x), wl_fixed_from_double(y));
    wl_display_roundtrip(display);

    if (strcmp(argv[1], "click") == 0 || strcmp(argv[1], "rightclick") == 0) {
        uint32_t btn = (strcmp(argv[1], "rightclick") == 0) ? BTN_RIGHT : BTN_LEFT;
        usleep(100000);
        org_kde_kwin_fake_input_button(fake_input, btn, 1);
        wl_display_roundtrip(display);
        usleep(50000);
        org_kde_kwin_fake_input_button(fake_input, btn, 0);
        wl_display_roundtrip(display);
    }

    wl_display_disconnect(display);
    return 0;
}
