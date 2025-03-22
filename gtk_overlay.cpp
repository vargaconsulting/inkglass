#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <iostream>

static GtkWidget *overlay_window;
static Display *x_display;
static Window selected_window;
static double transparency = 0.4;

void enable_input_passthrough(GdkWindow *gdk_window) {
    Display *x_display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Window x_overlay = GDK_WINDOW_XID(gdk_window);

    XserverRegion region = XFixesCreateRegion(x_display, NULL, 0);
    XFixesSetWindowShapeRegion(x_display, x_overlay, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(x_display, region);
}

Window select_x11_window(Display *x_display) {
    std::cout << "Click on a window to attach the overlay...\n";
    XEvent event;

    XGrabPointer(x_display, DefaultRootWindow(x_display), False, ButtonPressMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XMaskEvent(x_display, ButtonPressMask, &event);
    XUngrabPointer(x_display, CurrentTime);

    if (event.type == ButtonPress) {
        selected_window = event.xbutton.subwindow;
        if (selected_window == None) {
            selected_window = DefaultRootWindow(x_display);
        }

        XSelectInput(x_display, selected_window, StructureNotifyMask);
        return selected_window;
    }

    return None;
}

void update_overlay_position() {
    if (!selected_window) return;

    XWindowAttributes win_attr;
    XGetWindowAttributes(x_display, selected_window, &win_attr);

    int abs_x = 0, abs_y = 0;
    Window dummy;
    XTranslateCoordinates(x_display, selected_window, DefaultRootWindow(x_display),
                          0, 0, &abs_x, &abs_y, &dummy);

    gtk_window_move(GTK_WINDOW(overlay_window), abs_x, abs_y);
    gtk_window_resize(GTK_WINDOW(overlay_window), win_attr.width, win_attr.height);
}

void draw_overlay(GtkWidget *widget, cairo_t *cr, gpointer data) {
    cairo_set_source_rgba(cr, 0, 0, 0, transparency); // Semi-transparent black
    cairo_paint(cr);
}

gboolean configure_event_callback(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    update_overlay_position();
    return FALSE;
}

GtkWidget *create_overlay_window() {
    GtkWidget *overlay = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(overlay), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(overlay), TRUE);
    gtk_widget_set_app_paintable(overlay, TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(overlay), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);

    g_signal_connect(G_OBJECT(overlay), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(overlay));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(overlay, visual);

    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(overlay), drawing_area);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw_overlay), NULL);

    gtk_widget_show_all(overlay);

    enable_input_passthrough(gtk_widget_get_window(overlay));
    return overlay;
}

gboolean monitor_x11_window(gpointer user_data) {
    update_overlay_position();
    return G_SOURCE_CONTINUE;
}

gboolean handle_x11_events(GIOChannel *source, GIOCondition condition, gpointer data) {
    XEvent event;
    while (XPending(x_display)) {
        XNextEvent(x_display, &event);
        if (event.type == ConfigureNotify && event.xconfigure.window == selected_window) {
            update_overlay_position();
        }
    }
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    x_display = XOpenDisplay(NULL);
    if (!x_display) {
        std::cerr << "Failed to open X display\n";
        return 1;
    }

    if (select_x11_window(x_display) == None) {
        std::cerr << "No window selected!\n";
        return 1;
    }

    overlay_window = create_overlay_window();
    update_overlay_position(); // ensure it's positioned correctly from the start

    int x_fd = ConnectionNumber(x_display);
    GIOChannel *x_io_channel = g_io_channel_unix_new(x_fd);
    g_io_add_watch(x_io_channel, G_IO_IN, handle_x11_events, x_display);

    gtk_main();

    XCloseDisplay(x_display);
    return 0;
}
