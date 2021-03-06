/* exynos_drm_crtc.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _EXYNOS_DRM_CRTC_H_
#define _EXYNOS_DRM_CRTC_H_

#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"

struct exynos_drm_display;
struct exynos_drm_overlay;

int exynos_drm_crtc_create(struct drm_device *dev, unsigned int nr,
		struct exynos_drm_display *display);
int exynos_drm_crtc_enable_vblank(struct drm_device *dev, int crtc);
void exynos_drm_crtc_disable_vblank(struct drm_device *dev, int crtc);
void exynos_drm_overlay_disable(struct drm_crtc *crtc, int zpos);
void exynos_drm_crtc_apply(struct drm_crtc *crtc,
		struct exynos_drm_overlay *overlay);
void exynos_drm_crtc_finish_pageflip(struct drm_device *drm_dev, int crtc_idx);

/*
 * Exynos specific crtc postion structure.
 *
 * @fb_x: offset x on a framebuffer to be displyed
 *	- the unit is screen coordinates.
 * @fb_y: offset y on a framebuffer to be displayed
 *	- the unit is screen coordinates.
 * @fb_w: width of a framebuffer to be displayed
 *	- the unit is screen coordinates.
 * @fb_h: height of a framebuffer to be displayed
 *	- the unit is screen coordinates.
 * @crtc_x: offset x on hardware screen.
 * @crtc_y: offset y on hardware screen.
 * @crtc_w: width of hardware screen.
 * @crtc_h: height of hardware screen.
 */
struct exynos_drm_crtc_pos {
	unsigned int fb_x;
	unsigned int fb_y;
	unsigned int fb_w;
	unsigned int fb_h;
	unsigned int crtc_x;
	unsigned int crtc_y;
	unsigned int crtc_w;
	unsigned int crtc_h;
};

void exynos_drm_overlay_update(struct exynos_drm_overlay *overlay,
			      struct drm_framebuffer *fb,
			      struct drm_display_mode *mode,
			      struct exynos_drm_crtc_pos *pos);

void exynos_drm_kds_callback(void *callback_parameter, void *callback_extra_parameter);
#endif
