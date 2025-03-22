#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <iostream>
#include <functional>

// Global state
static GtkWidget *overlay_window;
static Display *x_display;
static Window selected_window;
static double transparency = 0.4;
const int toolbar_width = 36; // or 32 if you're feeling tight

// --- Button callbacks ---

void on_draw_button_clicked(GtkWidget *, gpointer) {
    std::cout << "Draw button pushed" << std::endl;
}

void on_erase_button_clicked(GtkWidget *, gpointer) {
    std::cout << "Erase button pushed" << std::endl;
}

// --- X11 overlay utilities ---

Window select_x11_window(Display *display) {
    std::cout << "Click on a window to attach the overlay...\n";
    XEvent event;

    XGrabPointer(display, DefaultRootWindow(display), False, ButtonPressMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XMaskEvent(display, ButtonPressMask, &event);
    XUngrabPointer(display, CurrentTime);

    if (event.type == ButtonPress) {
        Window target = event.xbutton.subwindow;
        if (target == None)
            target = DefaultRootWindow(display);

        XSelectInput(display, target, StructureNotifyMask);
        selected_window = target;
        return target;
    }

    return None;
}

void update_overlay_position() {
    if (!selected_window) return;

    XWindowAttributes attrs;
    XGetWindowAttributes(x_display, selected_window, &attrs);

    int abs_x = 0, abs_y = 0;
    Window dummy;
    XTranslateCoordinates(x_display, selected_window, DefaultRootWindow(x_display),
                          0, 0, &abs_x, &abs_y, &dummy);

    // Shift overlay left to make room for the toolbar
    gtk_window_move(GTK_WINDOW(overlay_window), abs_x - toolbar_width, abs_y);

    // Make the overlay window wider
    gtk_window_resize(GTK_WINDOW(overlay_window), attrs.width + toolbar_width, attrs.height);
}

// --- Drawing ---

void draw_overlay(GtkWidget *widget, cairo_t *cr, gpointer) {
    cairo_set_source_rgba(cr, 0, 0, 0, transparency); // semi-transparent black
    cairo_paint(cr);
}

// --- Create GTK overlay window ---

GtkWidget *create_overlay_window() {
    overlay_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(overlay_window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(overlay_window), TRUE);
    gtk_widget_set_app_paintable(overlay_window, TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(overlay_window), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);

    g_signal_connect(G_OBJECT(overlay_window), "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    // Enable RGBA visual for transparency
    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(overlay_window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual)
        gtk_widget_set_visual(overlay_window, visual);

    // Horizontal container: toolbar (left) + drawing area (right)
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    // Toolbar: vertical box on the left
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(hbox), toolbar, FALSE, FALSE, 0);

    GtkWidget *draw_btn = gtk_button_new_with_label("✎");
    GtkWidget *erase_btn = gtk_button_new_with_label("⌫");
    gtk_widget_set_size_request(draw_btn, 32, 32);
    gtk_widget_set_size_request(erase_btn, 32, 32);

    g_signal_connect(draw_btn, "clicked", G_CALLBACK(on_draw_button_clicked), nullptr);
    g_signal_connect(erase_btn, "clicked", G_CALLBACK(on_erase_button_clicked), nullptr);

    gtk_box_pack_start(GTK_BOX(toolbar), draw_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), erase_btn, FALSE, FALSE, 0);

    // Drawing area: fills the rest
    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_box_pack_start(GTK_BOX(hbox), drawing_area, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw_overlay), nullptr);

    // Add the container to the overlay window — only ONCE
    gtk_container_add(GTK_CONTAINER(overlay_window), hbox);

    gtk_widget_show_all(overlay_window);

    // Delay passthrough setup to exclude toolbar
    auto lambda = [=]() -> gboolean {
        GdkWindow *overlay_gdk = gtk_widget_get_window(overlay_window);
        GdkWindow *toolbar_gdk = gtk_widget_get_window(toolbar);

        if (!GDK_IS_X11_WINDOW(overlay_gdk) || !GDK_IS_X11_WINDOW(toolbar_gdk)) {
            std::cerr << "Failed to get valid X11 GdkWindows for passthrough setup.\n";
            return G_SOURCE_REMOVE;
        }

        Window x_overlay = GDK_WINDOW_XID(overlay_gdk);
        Display *display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

        int x, y;
        unsigned int w, h, border, depth;
        Window root;
        XGetGeometry(display, GDK_WINDOW_XID(toolbar_gdk),
                     &root, &x, &y, &w, &h, &border, &depth);

        XRectangle rect = {
            static_cast<short>(x), static_cast<short>(y),
            static_cast<unsigned short>(w),
            static_cast<unsigned short>(h)
        };

        XserverRegion region = XFixesCreateRegion(display, &rect, 1);
        XFixesSetWindowShapeRegion(display, x_overlay, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display, region);

        delete static_cast<std::function<gboolean()> *>(
            g_object_get_data(G_OBJECT(overlay_window), "passthrough-callback"));
        return G_SOURCE_REMOVE;
    };

    auto *callback = new std::function<gboolean()>(lambda);
    g_object_set_data(G_OBJECT(overlay_window), "passthrough-callback", callback);

    auto trampoline = [](gpointer data) -> gboolean {
        auto *func = static_cast<std::function<gboolean()> *>(data);
        return (*func)();
    };

    g_idle_add(trampoline, callback);
    return overlay_window;
}

// --- X11 event handling ---

gboolean handle_x11_events(GIOChannel *, GIOCondition, gpointer) {
    XEvent event;
    while (XPending(x_display)) {
        XNextEvent(x_display, &event);
        if (event.type == ConfigureNotify &&
            event.xconfigure.window == selected_window) {
            update_overlay_position();
        }
    }
    return G_SOURCE_CONTINUE;
}

// --- Main ---

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    x_display = XOpenDisplay(nullptr);
    if (!x_display) {
        std::cerr << "Failed to open X display\n";
        return 1;
    }

    if (select_x11_window(x_display) == None) {
        std::cerr << "No window selected!\n";
        return 1;
    }

    create_overlay_window();
    update_overlay_position();

    int x_fd = ConnectionNumber(x_display);
    GIOChannel *x_io = g_io_channel_unix_new(x_fd);
    g_io_add_watch(x_io, G_IO_IN, handle_x11_events, x_display);

    gtk_main();

    XCloseDisplay(x_display);
    return 0;
}
