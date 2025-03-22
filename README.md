# **Inkglass** - draw on anything

**Inkglass** is a GTK/X11-based overlay tool for Linux that lets you draw live annotations and stream your camera feed on top of any existing window — like an invisible glass sheet you can ink on. It's ideal for:

- Lecturers explaining content on-screen
- Streamers and VTubers wanting to annotate gameplay or demos
- Developers walking through code or tutorials

## Features

- **Live Drawing**: Supports Wacom-style pen tablets for smooth, real-time inking
- **Non-Intrusive Overlay**: Input-passthrough using XFixes keeps the target window interactive
- **Camera Feed Injection** (upcoming): Add your webcam stream as a PIP (picture-in-picture)
- **Streaming-Ready**: Record or broadcast the composite using a virtual camera (e.g. v4l2loopback)
- **Dynamic Attachment**: Overlay automatically resizes and follows the target X11 window

## Dependencies

- GTK 3
- X11 (libX11, Xfixes, Shape extension)
- Cairo
- (Optional) v4l2loopback for virtual camera output
- (Optional, planned) GStreamer or FFmpeg for compositing and recording

## Getting Started

```bash
git clone https://github.com/yourname/inkglass.git
cd inkglass
make  
./inkglass
```

## Roadmap

- Overlay attachment and passthrough
- Wacom pen support
- Live video feed compositing
- Virtual camera output
- Recording to file (via FFmpeg or GStreamer)
- Multi-window overlay support
- Brush tools and color picker

## How It Works
Inkglass creates a borderless GTK window layered above a selected X11 window. It:

- Uses XFixes and Shape extensions to allow mouse events to pass through
- Syncs size/position with the target window in real time
- Renders annotations via Cairo drawing
- Will optionally mix in a video feed and export the composite