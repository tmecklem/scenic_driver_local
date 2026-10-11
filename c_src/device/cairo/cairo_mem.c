/*
#  Added 2026-10-11 by Tim Mecklem's fork (not part of upstream Scenic.Driver.Local).
#  Licensed under the Apache License, Version 2.0, as the rest of this project.
#
#  The cairo-mem target: renders into memory another program can read.
#
#  The frame is a file (the driver's `window: [fbdev: path]` option) that this
#  program creates, sizes and maps: height rows of width pixels, four bytes a
#  pixel, cairo's ARGB32 in the machine's byte order (blue, green, red, alpha
#  on a little-endian machine), no padding. After each finished frame is
#  copied there, one byte is written to the named pipe `<path>.ready`, if a
#  reader has it open. The reader makes the pipe. A reader that looks while a
#  frame is being copied sees parts of two frames, and is told again when the
#  copy is done.
#
#  Nothing here looks at what changed between frames: that is the reader's.
#
#  Unlike the other targets, this one waits for its caller in poll() and uses
#  no processor time while nothing is drawn.
*/

#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "cairo_ctx.h"
#include "comms.h"
#include "device.h"
#include "image.h"
#include "scenic_ops.h"

typedef struct {
  uint8_t* frame;
  size_t size;
  char* ready_path;
  int ready_fd;
} cairo_mem_t;

static cairo_mem_t g_cairo_mem = {NULL, 0, NULL, -1};

extern device_opts_t g_opts;
extern device_info_t g_device_info;

int device_init(const device_opts_t* p_opts,
                device_info_t* p_info,
                driver_data_t* p_data)
{
  scenic_cairo_ctx_t* p_ctx = scenic_cairo_init(p_opts, p_info);
  if (!p_ctx) {
    return -1;
  }

  g_cairo_mem.size = (size_t)cairo_image_surface_get_stride(p_ctx->surface) *
                     cairo_image_surface_get_height(p_ctx->surface);

  int fd = open(g_opts.fbdev, O_RDWR | O_CREAT, 0600);
  if (fd == -1) {
    log_error("cairo-mem: cannot make %s: %s", g_opts.fbdev, strerror(errno));
    return -1;
  }

  g_cairo_mem.frame = MAP_FAILED;
  if (ftruncate(fd, g_cairo_mem.size) == 0) {
    g_cairo_mem.frame = mmap(NULL, g_cairo_mem.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  }
  close(fd);
  if (g_cairo_mem.frame == MAP_FAILED) {
    log_error("cairo-mem: cannot map %s: %s", g_opts.fbdev, strerror(errno));
    return -1;
  }

  // A frame left by an earlier run is not this run's to show.
  memset(g_cairo_mem.frame, 0, g_cairo_mem.size);

  g_cairo_mem.ready_path = malloc(strlen(g_opts.fbdev) + sizeof(".ready"));
  strcpy(g_cairo_mem.ready_path, g_opts.fbdev);
  strcat(g_cairo_mem.ready_path, ".ready");

  // A reader that goes away must not take this program with it.
  signal(SIGPIPE, SIG_IGN);

  return 0;
}

int device_close(device_info_t* p_info)
{
  munmap(g_cairo_mem.frame, g_cairo_mem.size);
  if (g_cairo_mem.ready_fd != -1) {
    close(g_cairo_mem.ready_fd);
  }
  free(g_cairo_mem.ready_path);

  scenic_cairo_fini((scenic_cairo_ctx_t*)p_info->v_ctx);
  return 0;
}

void device_poll()
{
}

void device_begin_render(driver_data_t* p_data)
{
  scenic_cairo_ctx_t* p_ctx = (scenic_cairo_ctx_t*)p_data->v_ctx;

  cairo_destroy(p_ctx->cr);
  p_ctx->cr = cairo_create(p_ctx->surface);

  cairo_set_source_rgba(p_ctx->cr,
                        p_ctx->clear_color.red,
                        p_ctx->clear_color.green,
                        p_ctx->clear_color.blue,
                        p_ctx->clear_color.alpha);
  cairo_paint(p_ctx->cr);
}

// Never waits: with no reader, or a reader that has not taken the last byte
// yet, there is nothing to add.
static void frame_ready()
{
  if (g_cairo_mem.ready_fd == -1) {
    g_cairo_mem.ready_fd = open(g_cairo_mem.ready_path, O_WRONLY | O_NONBLOCK);
    if (g_cairo_mem.ready_fd == -1) {
      return;
    }
  }

  if (write(g_cairo_mem.ready_fd, "\n", 1) == -1 && errno != EAGAIN) {
    close(g_cairo_mem.ready_fd);
    g_cairo_mem.ready_fd = -1;
  }
}

void device_end_render(driver_data_t* p_data)
{
  scenic_cairo_ctx_t* p_ctx = (scenic_cairo_ctx_t*)p_data->v_ctx;

  cairo_surface_flush(p_ctx->surface);
  memcpy(g_cairo_mem.frame, cairo_image_surface_get_data(p_ctx->surface), g_cairo_mem.size);
  frame_ready();
}

// scenic_loop looks at its input every 32 microseconds whether or not there
// is any, which costs a small device a fifth of a core while nothing is
// drawn. This waits for the caller instead, and then takes what it sent the
// same way.
void device_loop(driver_data_t* p_data)
{
  struct pollfd caller = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};

  send_ready();

  while (p_data->keep_going && !isCallerDown()) {
    if (poll(&caller, 1, -1) > 0 && (caller.revents & POLLIN)) {
      handle_stdio_in(p_data);
    }
  }

  reset_images(p_data->v_ctx);
  device_close(&g_device_info);
}
