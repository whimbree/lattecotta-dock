/*
 * SPDX-FileCopyrightText: 2026 Bree Spektor
 * SPDX-FileCopyrightText: 2026 Latte Dock contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * fakepointer - drive the KDE Wayland pointer AND keyboard via
 * org_kde_kwin_fake_input. Same mechanism KDE Connect uses for remote input;
 * same-user, no root. Used by the live-verification loop to hover dock items
 * (task previews, tooltips, parabolic zoom), click UI, and inject keys
 * (Escape to cancel an in-flight drag) without a human at the desk.
 *
 * usage: fakepointer move <x> <y>
 *        fakepointer click <x> <y>     (left click at absolute position)
 *        fakepointer middleclick <x> <y>
 *        fakepointer rightclick <x> <y>
 *        fakepointer drag <x1> <y1> <x2> <y2> [x3 y3 ...]  (left press, glide
 *          through every waypoint in one hold, release at the last)
 *        fakepointer glide <x1> <y1> <x2> <y2> [x3 y3 ...]  (same smooth
 *          motion stream, no buttons - replicates a real fast hover sweep)
 *        fakepointer scroll <x> <y> <detents> <ms-gap>
 *        fakepointer wheel <x> <y> <angle-delta>  (one vertical event in Qt
 *          angleDelta units; 120 is one wheel click)
 *        fakepointer key <keysym> [down|up|press]   (press = down then up, the
 *          default; down/up hold or release, e.g. a modifier. <keysym> is an
 *          XKB keysym NAME like Escape, Up, Return, space, or a numeric
 *          literal like 0xff1b.)
 *        fakepointer dragkey <keysym> <x1> <y1> <x2> <y2> [x3 y3 ...]  (left
 *          press at the first point, glide through every waypoint WITH THE
 *          BUTTON STILL HELD, tap <keysym> at the last waypoint, then release
 *          - all on ONE connection so the pointer grab spans the keystroke.
 *          The escape-within-a-held-pointer-drag primitive: a plain `key`
 *          run after a `drag` cannot reach an in-flight drag because `drag`
 *          has already released the button by the time the second process
 *          runs. Used to ask what Escape actually does to a Qt-Quick internal
 *          drag - e.g. the tasks-applet reorder, whose tasksModel.move is
 *          already applied live before the key ever arrives, DR-6 in
 *          docs/tracking/e2e-interaction-test-plan.md.)
 *
 * Why keysyms, not scancodes (the keymap question): the fake_input keyboard
 * axis offers two requests. keyboard_key(button,state) carries a Linux evdev
 * keycode whose resulting keysym depends on the compositor's active keymap and
 * layout - hardcoding one is fragile (it only stays stable for the
 * layout-independent keys, and can never reach a keysym that is not on the
 * active layout at all). keyboard_keysym(keysym,state) (since interface v6)
 * carries the XKB keysym directly and lets the compositor map it against ITS
 * OWN keymap: KWin's FakeInputBackend resolves the keysym with
 * keycodeFromKeysym, sets the shift/modifier level for us, and even
 * temporarily binds a spare keycode for a keysym absent from the active
 * layout. So we name the key by its layout-independent XKB keysym - resolved
 * from a name with libxkbcommon's xkb_keysym_from_name - and never assume the
 * compositor's keymap ourselves. That is the correct "handle the keymap"
 * choice: the semantic key is sent, the keycode lookup stays where the keymap
 * actually lives.
 *
 * A fake_input client holds keyboard/button state only while it is CONNECTED;
 * KWin releases anything still down when the client goes away. So a full
 * keystroke (down+up) rides one connection, as it does here. A `down`/`up`
 * pair for holding a modifier across a pointer drag cannot span two separate
 * invocations for the same reason - which is why the in-flight drag this verb
 * is built to cancel is KWin's keyboard interactive-move (a PERSISTENT
 * compositor mode that outlives any one input client), not a
 * pointer-button-held grab. See tests/e2e/080-key-escape-cancels-move.sh.
 *
 * build (inside the devshell):
 *   xml=$(pkg-config --variable=pkgdatadir plasma-wayland-protocols)/fake-input.xml
 *   wayland-scanner client-header "$xml" fake-input-client-protocol.h
 *   wayland-scanner private-code  "$xml" fake-input-protocol.c
 *   cc -O2 -o fakepointer fakepointer.c fake-input-protocol.c \
 *      $(pkg-config --cflags --libs wayland-client xkbcommon)
 *
 * KWin filters restricted globals per client: without authorization the
 * registry never lists fake_input. Authorize by installing a desktop file
 * in kwin's XDG_DATA_DIRS (e.g. ~/.local/share/applications) whose Exec is
 * the absolute path of this binary and which declares
 *   X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input
 * then run kbuildsycoca6. Same gate family as the dock's own
 * window-management/screencast access (org.kde.latte-dock.desktop).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "fake-input-client-protocol.h"

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

static struct org_kde_kwin_fake_input *fake_input = NULL;
//! the interface version actually bound, so each verb can check the version
//! it needs (absolute motion is since 3, keyboard_keysym since 6) instead of
//! assuming one blanket floor.
static uint32_t fake_input_version = 0;

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version)
{
    (void)data;
    if (strcmp(interface, org_kde_kwin_fake_input_interface.name) == 0) {
        uint32_t v = version < 6 ? version : 6;
        fake_input = wl_registry_bind(registry, name, &org_kde_kwin_fake_input_interface, v);
        fake_input_version = v;
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

//! Resolve a key NAME (an XKB keysym name like "Escape", "Return", "space")
//! to its XKB keysym value, falling back to a numeric literal ("0xff1b",
//! "65307") so a caller can always name a key this tool has no shorthand for.
//! Returns XKB_KEY_NoSymbol (0) when neither form resolves.
static xkb_keysym_t resolve_keysym(const char *name)
{
    xkb_keysym_t keysym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
    if (keysym != XKB_KEY_NoSymbol) {
        return keysym;
    }
    char *end = NULL;
    unsigned long numeric = strtoul(name, &end, 0);
    if (end != name && *end == '\0' && numeric != 0) {
        return (xkb_keysym_t)numeric;
    }
    return XKB_KEY_NoSymbol;
}

int main(int argc, char **argv)
{
    int isdrag = (argc > 1) && (strcmp(argv[1], "drag") == 0);
    int isglide = (argc > 1) && (strcmp(argv[1], "glide") == 0);
    int isscroll = (argc > 1) && (strcmp(argv[1], "scroll") == 0);
    int iswheel = (argc > 1) && (strcmp(argv[1], "wheel") == 0);
    int iskey = (argc > 1) && (strcmp(argv[1], "key") == 0);
    int isdragkey = (argc > 1) && (strcmp(argv[1], "dragkey") == 0);

    if (iskey) {
        if (argc != 3 && argc != 4) {
            fprintf(stderr, "usage: %s key <keysym> [down|up|press]\n", argv[0]);
            return 2;
        }
    } else if (isdragkey) {
        //! dragkey <keysym> <x1> <y1> <x2> <y2> [...]: keysym then >=2 coord
        //! pairs, so argc is odd and at least 7 (prog dragkey key x1 y1 x2 y2)
        if (argc < 7 || (argc % 2) == 0) {
            fprintf(stderr, "usage: %s dragkey <keysym> <x1> <y1> <x2> <y2> [x3 y3 ...]\n"
                            "  press at (x1,y1), glide through the waypoints with the button held,\n"
                            "  tap <keysym> at the last waypoint, then release - one held-drag session\n",
                    argv[0]);
            return 2;
        }
    } else if (((isdrag || isglide) && (argc < 6 || (argc % 2) != 0))
        || (isscroll && argc != 6)
        || (iswheel && argc != 5)
        || (!isdrag && !isglide && !isscroll && !iswheel
            && (argc != 4 || (strcmp(argv[1], "move") && strcmp(argv[1], "click") && strcmp(argv[1], "middleclick") && strcmp(argv[1], "rightclick"))))) {
        fprintf(stderr, "usage: %s move|click|middleclick|rightclick <x> <y>  |  %s drag|glide <x1> <y1> <x2> <y2> [x3 y3 ...]  |  %s scroll <x> <y> <detents> <ms-gap>  |  %s wheel <x> <y> <angle-delta>  |  %s key <keysym> [down|up|press]  |  %s dragkey <keysym> <x1> <y1> <x2> <y2> [x3 y3 ...]\n"
                        "  scroll: positive detents scroll up, negative down; one detent = one wheel click\n"
                        "  wheel:  signed vertical Qt angleDelta for one event; useful for threshold checks\n"
                        "  key:    <keysym> is an XKB name (Escape, Up, Return, space) or a numeric literal; state defaults to press (down then up)\n"
                        "  dragkey: press, glide (button held), tap <keysym> at the last point, release - one held-drag session\n",
                argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }

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

    if (iskey) {
        //! keyboard_keysym is a v6 request; a compositor advertising less can
        //! carry pointer events but not a keysym, so say which is missing.
        if (fake_input_version < 6) {
            fprintf(stderr, "org_kde_kwin_fake_input version %u < 6, no keyboard_keysym (key injection needs v6)\n",
                fake_input_version);
            wl_display_disconnect(display);
            return 3;
        }
        xkb_keysym_t keysym = resolve_keysym(argv[2]);
        if (keysym == XKB_KEY_NoSymbol) {
            fprintf(stderr, "unknown keysym '%s' (try an XKB name like Escape, or a numeric literal like 0xff1b)\n",
                argv[2]);
            wl_display_disconnect(display);
            return 2;
        }
        const char *state = (argc == 4) ? argv[3] : "press";
        int do_press, do_release;
        if (strcmp(state, "press") == 0) {
            do_press = 1; do_release = 1;
        } else if (strcmp(state, "down") == 0) {
            do_press = 1; do_release = 0;
        } else if (strcmp(state, "up") == 0) {
            do_press = 0; do_release = 1;
        } else {
            fprintf(stderr, "unknown key state '%s' (want down, up, or press)\n", state);
            wl_display_disconnect(display);
            return 2;
        }

        if (do_press) {
            org_kde_kwin_fake_input_keyboard_keysym(fake_input, keysym, WL_KEYBOARD_KEY_STATE_PRESSED);
            wl_display_roundtrip(display);
        }
        if (do_press && do_release) {
            usleep(20000);
        }
        if (do_release) {
            org_kde_kwin_fake_input_keyboard_keysym(fake_input, keysym, WL_KEYBOARD_KEY_STATE_RELEASED);
            wl_display_roundtrip(display);
        }

        wl_display_disconnect(display);
        return 0;
    }

    if (isdragkey) {
        //! keysym goes to the compositor's keymap the same way `key` does, so
        //! it needs the same v6 floor; a lower version can still carry the
        //! pointer half but not the keystroke, so refuse rather than press-drag
        //! silently without the key the caller asked for.
        if (fake_input_version < 6) {
            fprintf(stderr, "org_kde_kwin_fake_input version %u < 6, no keyboard_keysym (dragkey needs v6)\n",
                fake_input_version);
            wl_display_disconnect(display);
            return 3;
        }
        xkb_keysym_t keysym = resolve_keysym(argv[2]);
        if (keysym == XKB_KEY_NoSymbol) {
            fprintf(stderr, "unknown keysym '%s' (try an XKB name like Escape, or a numeric literal like 0xff1b)\n",
                argv[2]);
            wl_display_disconnect(display);
            return 2;
        }

        const int steps = 24;
        double x = atof(argv[3]);
        double y = atof(argv[4]);

        //! press at the first point, then hold the button through every glide
        //! and the keystroke - the whole point of this verb is that the grab
        //! outlives the key, which a separate `drag` then `key` cannot do
        org_kde_kwin_fake_input_pointer_motion_absolute(fake_input,
            wl_fixed_from_double(x), wl_fixed_from_double(y));
        wl_display_roundtrip(display);
        usleep(100000);
        org_kde_kwin_fake_input_button(fake_input, BTN_LEFT, 1);
        wl_display_roundtrip(display);
        usleep(150000);

        double cx = x, cy = y;
        for (int w = 5; w + 1 < argc; w += 2) {
            double wx = atof(argv[w]);
            double wy = atof(argv[w + 1]);
            for (int i = 1; i <= steps; ++i) {
                double sx = cx + (wx - cx) * i / steps;
                double sy = cy + (wy - cy) * i / steps;
                org_kde_kwin_fake_input_pointer_motion_absolute(fake_input,
                    wl_fixed_from_double(sx), wl_fixed_from_double(sy));
                wl_display_roundtrip(display);
                usleep(12000);
            }
            cx = wx;
            cy = wy;
        }

        //! tap the key at the target with the button still down
        usleep(120000);
        org_kde_kwin_fake_input_keyboard_keysym(fake_input, keysym, WL_KEYBOARD_KEY_STATE_PRESSED);
        wl_display_roundtrip(display);
        usleep(20000);
        org_kde_kwin_fake_input_keyboard_keysym(fake_input, keysym, WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_display_roundtrip(display);
        usleep(120000);

        org_kde_kwin_fake_input_button(fake_input, BTN_LEFT, 0);
        wl_display_roundtrip(display);

        wl_display_disconnect(display);
        return 0;
    }

    double x = atof(argv[2]);
    double y = atof(argv[3]);

    org_kde_kwin_fake_input_pointer_motion_absolute(fake_input,
        wl_fixed_from_double(x), wl_fixed_from_double(y));
    wl_display_roundtrip(display);

    if (strcmp(argv[1], "click") == 0 || strcmp(argv[1], "middleclick") == 0 || strcmp(argv[1], "rightclick") == 0) {
        uint32_t btn = BTN_LEFT;
        if (strcmp(argv[1], "middleclick") == 0) {
            btn = BTN_MIDDLE;
        } else if (strcmp(argv[1], "rightclick") == 0) {
            btn = BTN_RIGHT;
        }
        usleep(100000);
        org_kde_kwin_fake_input_button(fake_input, btn, 1);
        wl_display_roundtrip(display);
        usleep(50000);
        org_kde_kwin_fake_input_button(fake_input, btn, 0);
        wl_display_roundtrip(display);
    } else if (isdrag || isglide) {
        //! press at (x,y), glide through every waypoint in small steps so
        //! drag handlers see a believable motion stream, release at the last.
        //! Step pacing mirrors a human drag because drop targets debounce
        //! hover. glide is the same stream without buttons.
        const int steps = 24;

        if (isdrag) {
            usleep(100000);
            org_kde_kwin_fake_input_button(fake_input, BTN_LEFT, 1);
            wl_display_roundtrip(display);
            usleep(150000);
        }

        double cx = x, cy = y;

        for (int w = 4; w + 1 < argc; w += 2) {
            double wx = atof(argv[w]);
            double wy = atof(argv[w + 1]);

            for (int i = 1; i <= steps; ++i) {
                double sx = cx + (wx - cx) * i / steps;
                double sy = cy + (wy - cy) * i / steps;
                org_kde_kwin_fake_input_pointer_motion_absolute(fake_input,
                    wl_fixed_from_double(sx), wl_fixed_from_double(sy));
                wl_display_roundtrip(display);
                usleep(12000);
            }

            cx = wx;
            cy = wy;
        }

        if (isdrag) {
            usleep(150000);
            org_kde_kwin_fake_input_button(fake_input, BTN_LEFT, 0);
            wl_display_roundtrip(display);
        }
    } else if (isscroll) {
        //! wheel detents at (x,y). fake_input axis takes wl_pointer.axis
        //! units: 15 per detent (Qt multiplies by 8 into angleDelta 120).
        //! Sign: positive wayland axis = scroll DOWN, so a positive detent
        //! count (scroll up, volume up by convention) sends negative values.
        //! The per-detent gap approximates a human scroll rate so debounce
        //! and accumulation behaviors face a realistic stream.
        int detents = atoi(argv[4]);
        int gapms = atoi(argv[5]);
        int dir = (detents >= 0) ? -1 : 1;
        int n = (detents >= 0) ? detents : -detents;

        usleep(100000);

        for (int i = 0; i < n; ++i) {
            org_kde_kwin_fake_input_axis(fake_input, 0 /* vertical */,
                wl_fixed_from_double(15.0 * dir));
            wl_display_roundtrip(display);
            usleep((useconds_t)gapms * 1000);
        }
    } else if (iswheel) {
        //! KWin/Qt multiply Wayland axis units by -8 into angleDelta.
        char *end = NULL;
        const double angle_delta = strtod(argv[4], &end);
        if (end == argv[4] || *end != '\0' || !isfinite(angle_delta)) {
            fprintf(stderr, "invalid angle-delta '%s' (want a finite number)\n", argv[4]);
            wl_display_disconnect(display);
            return 2;
        }
        org_kde_kwin_fake_input_axis(fake_input, 0 /* vertical */,
            wl_fixed_from_double(-angle_delta / 8.0));
        wl_display_roundtrip(display);
    }

    wl_display_disconnect(display);
    return 0;
}
