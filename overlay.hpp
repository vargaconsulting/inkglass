#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <cairo/cairo-xlib.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <algorithm>

struct overlay_t {
    overlay_t() {
        display = XOpenDisplay(NULL);
        if (!display) {
            std::cerr << "Failed to open X display\n";
            exit(1);
        }
        root = DefaultRootWindow(display);
    }

    ~overlay_t() {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        XDestroyWindow(display, overlay);
        XCloseDisplay(display);
    }

    void set_opacity(Window win, double opacity) {
        Atom opacity_atom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
        unsigned long opacity_value = (unsigned long)(opacity * 0xFFFFFFFF);
        XChangeProperty(display, win, opacity_atom, XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char*)&opacity_value, 1);
    }

    void draw_overlay() {
        cairo_set_source_rgba(cr, 0, 0, 0, transparency);
        cairo_rectangle(cr, 0, 0, overlay_width, overlay_height);
        cairo_fill(cr);
        cairo_surface_flush(surface);
    }

    Window select_window() {
        std::cout << "Click on a window to attach the overlay...\n";

        Window selected_window = None;
        XEvent event;

        XGrabPointer(display, root, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        XMaskEvent(display, ButtonPressMask, &event);
        XUngrabPointer(display, CurrentTime);

        if (event.type == ButtonPress) {
            selected_window = event.xbutton.subwindow;
            if (selected_window == None) {
                selected_window = root;
            }
        }
        return selected_window;
    }

    void update_size() {
        XWindowAttributes win_attr;
        XGetWindowAttributes(display, target_window, &win_attr);

        overlay_width = win_attr.width;
        overlay_height = win_attr.height;

        XMoveResizeWindow(display, overlay, win_attr.x, win_attr.y, overlay_width, overlay_height);
        set_opacity(overlay, transparency);

        cairo_destroy(cr);
        cairo_surface_destroy(surface);

        surface = cairo_xlib_surface_create(display, overlay, overlay_visual, overlay_width, overlay_height);
        cr = cairo_create(surface);

        draw_overlay();
    }

    void create_overlay(Window target) {
        target_window = target;
        XWindowAttributes win_attr;
        XGetWindowAttributes(display, target_window, &win_attr);
    
        overlay_width = win_attr.width;
        overlay_height = win_attr.height;
    
        XVisualInfo vinfo;
        if (!XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &vinfo)) {
            std::cerr << "No 32-bit ARGB visual found, falling back to default visual.\n";
            if (!XMatchVisualInfo(display, DefaultScreen(display), 24, TrueColor, &vinfo)) {
                std::cerr << "No suitable visual found! Exiting...\n";
                exit(1);
            }
        }
    
        overlay_visual = vinfo.visual;
        overlay_depth = vinfo.depth;
    
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        attrs.colormap = XCreateColormap(display, root, overlay_visual, AllocNone);
        attrs.background_pixel = 0;
        attrs.border_pixel = 0;
        attrs.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;
    
        overlay = XCreateWindow(display, root, win_attr.x, win_attr.y, overlay_width, overlay_height, 0, overlay_depth, InputOutput,
                                overlay_visual, CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect | CWEventMask, &attrs);
    
        set_opacity(overlay, transparency);
    
        // Enable transparency and input pass-through
        XserverRegion region = XFixesCreateRegion(display, NULL, 0);
        XFixesSetWindowShapeRegion(display, overlay, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display, region);
    
        // Listen for resize & move events on the **target window** (not just overlay)
        XSelectInput(display, target_window, StructureNotifyMask | KeyPressMask);
    
        XMapWindow(display, overlay);
        surface = cairo_xlib_surface_create(display, overlay, overlay_visual, overlay_width, overlay_height);
        cr = cairo_create(surface);
    
        draw_overlay();
    }

    void toggle_overlay() {
        if (overlay_visible) {
            XUnmapWindow(display, overlay);
        } else {
            XMapWindow(display, overlay);
            set_opacity(overlay, last_transparency);
            draw_overlay();
            XFlush(display);
        }
        overlay_visible = !overlay_visible;
    }

    void register_hotkeys() {
        KeyCode toggle_key = XKeysymToKeycode(display, XStringToKeysym("o"));
        KeyCode exit_key = XKeysymToKeycode(display, XStringToKeysym("q"));
        
        XGrabKey(display, toggle_key, Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(display, exit_key, Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);
    }

    void run() {
        target_window = select_window();
    
        if (target_window == None) {
            std::cerr << "No window selected!\n";
            return;
        }
    
        create_overlay(target_window);
        register_hotkeys();
    
        XEvent event;
        while (true) {
            XNextEvent(display, &event);
    
            if (event.type == Expose)
                draw_overlay();
    
            if (event.type == ConfigureNotify && event.xconfigure.window == target_window) {
                update_size();  // Now this updates when target window moves or resizes
            }
    
            if (event.type == KeyPress) {
                KeySym key = XLookupKeysym(&event.xkey, 0);
                if (key == XStringToKeysym("o")) {
                    toggle_overlay();
                }
                if (key == XStringToKeysym("q")) {
                    std::cout << "Exiting program...\n";
                    break;
                }
            }
        }
    }

    Display *display;
    Window root, overlay, target_window;
    cairo_surface_t *surface;
    cairo_t *cr;
    int overlay_width, overlay_height;
    Visual *overlay_visual;
    int overlay_depth;
    bool overlay_visible = true;
    double transparency = 0.4; // Configurable transparency value
    double last_transparency = 0.4; // Stores last used transparency
};
