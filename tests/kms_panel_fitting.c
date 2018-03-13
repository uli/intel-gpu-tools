/*
 * Copyright © 2013,2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "igt.h"
#include <math.h>
#include <sys/stat.h>

IGT_TEST_DESCRIPTION("Test display panel fitting");

typedef struct {
	int drm_fd;
	igt_display_t display;

	struct igt_fb fb1;
	struct igt_fb fb2;

	igt_plane_t *plane1;
	igt_plane_t *plane2;
	igt_plane_t *plane3;
	igt_plane_t *plane4;
} data_t;

#define FILE_NAME   "1080p-left.png"

static void prepare_crtc(data_t *data, igt_output_t *output, enum pipe pipe,
			igt_plane_t *plane, drmModeModeInfo *mode, enum igt_commit_style s)
{
	igt_display_t *display = &data->display;

	igt_output_set_pipe(output, pipe);

	/* before allocating, free if any older fb */
	igt_remove_fb(data->drm_fd, &data->fb1);

	/* allocate fb for plane 1 */
	igt_create_pattern_fb(data->drm_fd,
			      mode->hdisplay, mode->vdisplay,
			      DRM_FORMAT_XRGB8888,
			      LOCAL_DRM_FORMAT_MOD_NONE,
			      &data->fb1);

	/*
	 * We always set the primary plane to actually enable the pipe as
	 * there's no way (that works) to light up a pipe with only a sprite
	 * plane enabled at the moment.
	 */
	if (plane->type != DRM_PLANE_TYPE_PRIMARY) {
		igt_plane_t *primary;

		primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
		igt_plane_set_fb(primary, &data->fb1);
	}

	igt_plane_set_fb(plane, &data->fb1);
	igt_display_commit2(display, s);
}

static void cleanup_crtc(data_t *data, igt_output_t *output, igt_plane_t *plane)
{
	igt_display_t *display = &data->display;

	igt_remove_fb(data->drm_fd, &data->fb1);
	igt_remove_fb(data->drm_fd, &data->fb2);

	if (plane->type != DRM_PLANE_TYPE_PRIMARY) {
		igt_plane_t *primary;

		primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
		igt_plane_set_fb(primary, NULL);
	}

	igt_plane_set_fb(plane, NULL);
	igt_output_set_pipe(output, PIPE_ANY);

	igt_display_commit2(display, COMMIT_UNIVERSAL);
}

static void test_panel_fitting(data_t *d)
{
	igt_display_t *display = &d->display;
	igt_output_t *output;
	enum pipe pipe;
	int valid_tests = 0;

	for_each_pipe_with_valid_output(display, pipe, output) {
		drmModeModeInfo *mode, native_mode;
		bool scaling_mode_set;

		scaling_mode_set = kmstest_get_property(d->drm_fd,
			output->config.connector->connector_id,
			DRM_MODE_OBJECT_CONNECTOR,
			"scaling mode",
			NULL,
			NULL,
			NULL);

		/* Check that the "scaling mode" property has been set. */
		if (!scaling_mode_set)
			continue;

		igt_output_set_pipe(output, pipe);

		mode = igt_output_get_mode(output);
		native_mode = *mode;

		/* allocate fb2 with image */
		igt_create_image_fb(d->drm_fd, 0, 0,
				    DRM_FORMAT_XRGB8888,
				    LOCAL_DRM_FORMAT_MOD_NONE,
				    FILE_NAME, &d->fb2);

		/* Set up display to enable panel fitting */
		mode->hdisplay = 640;
		mode->vdisplay = 480;
		d->plane1 = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
		prepare_crtc(d, output, pipe, d->plane1, mode, COMMIT_LEGACY);

		/* disable panel fitting */
		prepare_crtc(d, output, pipe, d->plane1, &native_mode, COMMIT_LEGACY);

		/* enable panel fitting */
		mode->hdisplay = 800;
		mode->vdisplay = 600;
		prepare_crtc(d, output, pipe, d->plane1, mode, COMMIT_LEGACY);

		/* disable panel fitting */
		prepare_crtc(d, output, pipe, d->plane1, &native_mode, COMMIT_LEGACY);

		/* Set up fb2->plane2 mapping. */
		d->plane2 = igt_output_get_plane_type(output, DRM_PLANE_TYPE_OVERLAY);
		igt_plane_set_fb(d->plane2, &d->fb2);

		/* enable sprite plane */
		igt_fb_set_position(&d->fb2, d->plane2, 100, 100);
		igt_fb_set_size(&d->fb2, d->plane2, d->fb2.width-200, d->fb2.height-200);
		igt_plane_set_position(d->plane2, 100, 100);
		igt_plane_set_size(d->plane2, mode->hdisplay-200, mode->vdisplay-200);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		/* enable panel fitting along with sprite scaling */
		mode->hdisplay = 1024;
		mode->vdisplay = 768;
		prepare_crtc(d, output, pipe, d->plane1, mode, COMMIT_LEGACY);

		/* back to single plane mode */
		igt_plane_set_fb(d->plane2, NULL);
		igt_display_commit2(display, COMMIT_UNIVERSAL);

		valid_tests++;
		cleanup_crtc(d, output, d->plane1);
	}
	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

static void
test_panel_fitting_fastset(igt_display_t *display, const enum pipe pipe, igt_output_t *output)
{
	igt_plane_t *primary, *sprite;
	drmModeModeInfo mode;
	struct igt_fb red, green, blue;

	igt_assert(kmstest_get_connector_default_mode(display->drm_fd, output->config.connector, &mode));

	igt_output_override_mode(output, &mode);
	igt_output_set_pipe(output, pipe);

	primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
	sprite = igt_output_get_plane_type(output, DRM_PLANE_TYPE_OVERLAY);

	igt_create_color_fb(display->drm_fd, mode.hdisplay, mode.vdisplay,
			    DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE,
			    0.f, 0.f, 1.f, &blue);

	igt_create_color_fb(display->drm_fd, 640, 480,
			    DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE,
			    1.f, 0.f, 0.f, &red);

	igt_create_color_fb(display->drm_fd, 800, 600,
			    DRM_FORMAT_XRGB8888, LOCAL_DRM_FORMAT_MOD_NONE,
			    0.f, 1.f, 0.f, &green);

	igt_plane_set_fb(primary, &blue);
	igt_plane_set_fb(sprite, &red);

	igt_display_commit2(display, COMMIT_ATOMIC);

	mode.hdisplay = 640;
	mode.vdisplay = 480;
	igt_output_override_mode(output, &mode);

	igt_plane_set_fb(sprite, NULL);
	igt_plane_set_fb(primary, &red);

	/* Don't pass ALLOW_MODESET with overridden mode, force fastset. */
	igt_display_commit_atomic(display, 0, NULL);

	/* Test with different scaled mode */
	mode.hdisplay = 800;
	mode.vdisplay = 600;
	igt_output_override_mode(output, &mode);
	igt_plane_set_fb(primary, &green);
	igt_display_commit_atomic(display, 0, NULL);

	/* Restore normal mode */
	igt_plane_set_fb(primary, &blue);
	igt_output_override_mode(output, NULL);
	igt_display_commit_atomic(display, 0, NULL);

	igt_plane_set_fb(primary, NULL);
	igt_output_set_pipe(output, PIPE_NONE);
}

static void test_atomic_fastset(igt_display_t *display)
{
	igt_output_t *output;
	enum pipe pipe;
	int valid_tests = 0;
	struct stat sb;

	/* Until this is force enabled, force modeset evasion. */
	if (stat("/sys/module/i915/parameters/fastboot", &sb) == 0)
		igt_set_module_param_int("fastboot", 1);

	igt_require(display->is_atomic);
	igt_require(is_i915_device(display->drm_fd));
	igt_require(intel_gen(intel_get_drm_devid(display->drm_fd)) >= 5);

	for_each_pipe_with_valid_output(display, pipe, output) {
		if (!output->props[IGT_CONNECTOR_SCALING_MODE])
			continue;

		test_panel_fitting_fastset(display, pipe, output);
		valid_tests++;
	}
	igt_require_f(valid_tests, "no valid crtc/connector combinations found\n");
}

igt_main
{
	data_t data = {};

	igt_fixture {
		igt_skip_on_simulation();

		data.drm_fd = drm_open_driver(DRIVER_ANY);
		igt_display_init(&data.display, data.drm_fd);
	}

	igt_subtest("legacy")
		test_panel_fitting(&data);

	igt_subtest("atomic-fastset")
		test_atomic_fastset(&data.display);

	igt_fixture
		igt_display_fini(&data.display);
}
