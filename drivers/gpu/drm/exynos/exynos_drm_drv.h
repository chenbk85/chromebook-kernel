/* exynos_drm_drv.h
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

#ifndef _EXYNOS_DRM_DRV_H_
#define _EXYNOS_DRM_DRV_H_

#include <linux/kref.h>
#include <linux/module.h>
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"

#ifdef CONFIG_EXYNOS_IOMMU
#include <mach/sysmmu.h>
#include <linux/of_platform.h>
#endif

#ifdef CONFIG_DMA_SHARED_BUFFER_USES_KDS
#include "linux/kds.h"
#include <linux/dma-buf.h>
#endif

#define DRM_BASE_ID(obj)	((obj) ? (obj)->base.id : -1)
#define subdrv_name(s)  ((s) ? exynos_display_type_name((s)->display) : "none")

#define MAX_PLANE	5
#define MAX_FB_BUFFER	4
#define DEFAULT_ZPOS	-1

struct drm_device;
struct exynos_drm_overlay;
struct drm_connector;
struct exynos_drm_gem_object;

extern unsigned int drm_vblank_offdelay;

/* The different display types that are supported by the exynos drm driver */
enum exynos_drm_display_type {
	EXYNOS_DRM_DISPLAY_TYPE_FIMD = 0,
	EXYNOS_DRM_DISPLAY_TYPE_MIXER,
	EXYNOS_DRM_DISPLAY_TYPE_VIDI,
	EXYNOS_DRM_DISPLAY_NUM_DISPLAYS,
};

/*
 * Exynos drm common overlay structure.
 *
 * @fb_x: offset x on a framebuffer to be displayed.
 *	- the unit is screen coordinates.
 * @fb_y: offset y on a framebuffer to be displayed.
 *	- the unit is screen coordinates.
 * @fb_width: width of a framebuffer.
 * @fb_height: height of a framebuffer.
 * @fb_pitch: pitch of a framebuffer.
 * @crtc_x: offset x on hardware screen.
 * @crtc_y: offset y on hardware screen.
 * @crtc_width: window width to be displayed (hardware screen).
 * @crtc_height: window height to be displayed (hardware screen).
 * @mode_width: width of screen mode.
 * @mode_height: height of screen mode.
 * @refresh: refresh rate.
 * @scan_flag: interlace or progressive way.
 *	(it could be DRM_MODE_FLAG_*)
 * @bpp: pixel size.(in bit)
 * @pixel_format: fourcc pixel format of this overlay
 * @dma_addr: array of bus(accessed by dma) address to the memory region
 *	      allocated for a overlay.
 * @zpos: order of overlay layer(z position).
 * @default_win: a window to be enabled.
 * @color_key: color key on or off.
 * @index_color: if using color key feature then this value would be used
 *			as index color.
 * @local_path: in case of lcd type, local path mode on or off.
 * @transparency: transparency on or off.
 * @activated: activated or not.
 *
 * this structure is common to exynos SoC and its contents would be copied
 * to hardware specific overlay info.
 */
struct exynos_drm_overlay {
	unsigned int fb_x;
	unsigned int fb_y;
	unsigned int fb_width;
	unsigned int fb_height;
	unsigned int fb_pitch;
	unsigned int crtc_x;
	unsigned int crtc_y;
	unsigned int crtc_width;
	unsigned int crtc_height;
	unsigned int crtc_htotal;
	unsigned int crtc_hsync_len;
	unsigned int crtc_vtotal;
	unsigned int crtc_vsync_len;
	unsigned int mode_width;
	unsigned int mode_height;
	unsigned int refresh;
	unsigned int scan_flag;
	unsigned int bpp;
	uint32_t pixel_format;
	dma_addr_t dma_addr[MAX_FB_BUFFER];
	int zpos;

	bool default_win;
	bool color_key;
	unsigned int index_color;
	bool local_path;
	bool transparency;
	bool activated;
};

/*
 * Exynos drm private structure.
 */
struct exynos_drm_private {
	struct drm_fb_helper *fb_helper;

	wait_queue_head_t wait_vsync_queue;

	/*
	 * created crtc object would be contained at this array and
	 * this array is used to be aware of which crtc did it request vblank.
	 */
	struct drm_crtc *crtc[EXYNOS_DRM_DISPLAY_NUM_DISPLAYS];
	struct drm_property *plane_zpos_property;

#ifdef CONFIG_DMA_SHARED_BUFFER_USES_KDS
	struct kds_callback kds_cb;
#endif
#ifdef CONFIG_DRM_EXYNOS_DEBUG
	struct {
		atomic_t object_memory;
		atomic_t object_count;
	} mm;
#endif
};

/*
 * Exynos drm_file private structure.
 *
 * @gem_cpu_acquire_list: list of GEM objects we hold acquires on
 */
struct exynos_drm_file_private {
	struct list_head gem_cpu_acquire_list;
};

/*
 * Exynos drm sub driver structure.
 *
 * @list: sub driver has its own list object to register to exynos drm driver.
 * @dev: pointer to device object for subdrv device driver.
 * @drm_dev: pointer to drm_device and this pointer would be set
 *	when sub driver calls exynos_drm_subdrv_register().
 * @display: A pointer to the display structure for this sub driver
 * @probe: this callback would be called by exynos drm driver after
 *	subdrv is registered to it.
 * @remove: this callback is used to release resources created
 *	by probe callback.
 * @open: this would be called with drm device file open.
 * @close: this would be called with drm device file close.
 * @encoder: encoder object owned by this sub driver.
 * @connector: connector object owned by this sub driver.
 */
struct exynos_drm_subdrv {
	struct list_head list;
	struct device *dev;
	struct drm_device *drm_dev;
	struct exynos_drm_display *display;

	int (*probe)(struct drm_device *drm_dev,
			struct exynos_drm_subdrv *subdrv);
	void (*remove)(struct drm_device *dev);
	int (*open)(struct drm_device *drm_dev, struct device *dev,
			struct drm_file *file);
	void (*close)(struct drm_device *drm_dev, struct device *dev,
			struct drm_file *file);

	struct drm_encoder *encoder;
	struct drm_connector *connector;
};

/*
 * this function calls a probe callback registered to sub driver list and
 * create its own encoder and connector and then set drm_device object
 * to global one.
 */
int exynos_drm_device_register(struct drm_device *dev);
/*
 * this function calls a remove callback registered to sub driver list and
 * destroy its own encoder and connetor.
 */
int exynos_drm_device_unregister(struct drm_device *dev);

/*
 * this function would be called by sub drivers such as display controller
 * or hdmi driver to register this sub driver object to exynos drm driver
 * and when a sub driver is registered to exynos drm driver a probe callback
 * of the sub driver is called and creates its own encoder and connector.
 */
int exynos_drm_subdrv_register(struct exynos_drm_subdrv *drm_subdrv);

/* this function removes subdrv list from exynos drm driver */
int exynos_drm_subdrv_unregister(struct exynos_drm_subdrv *drm_subdrv);

void exynos_fimd_dp_attach(struct device *dev);

int exynos_drm_subdrv_open(struct drm_device *dev, struct drm_file *file);
void exynos_drm_subdrv_close(struct drm_device *dev, struct drm_file *file);

void exynos_drm_encoder_dpms(struct drm_encoder *encoder, int mode);

/*
 * exynos specific framebuffer structure.
 *
 * @release_work: for the worker destroying the fb after it's done being used
 * @fb: drm framebuffer obejct.
 * @exynos_gem_obj: array of exynos specific gem object containing a gem object.
 * @rendered: true once fb is rendered
 * @prepared: true once shadow registers have been updated
 */
struct exynos_drm_fb {
	struct kref			refcount;
	struct work_struct		release_work;
	struct drm_framebuffer		fb;
	struct exynos_drm_gem_obj	*exynos_gem_obj[MAX_FB_BUFFER];
#ifdef CONFIG_DMA_SHARED_BUFFER_USES_KDS
	struct kds_resource_set		*kds_res_set;
	struct dma_buf			*dma_buf;
#endif
	bool				rendered;
	bool				prepared;
};

#define to_exynos_fb(x)	container_of(x, struct exynos_drm_fb, fb)

void exynos_drm_fb_release(struct kref *kref);

static inline void exynos_drm_fb_get(struct exynos_drm_fb *exynos_fb)
{
	kref_get(&exynos_fb->refcount);
}

static inline void exynos_drm_fb_put(struct exynos_drm_fb *exynos_fb)
{
	kref_put(&exynos_fb->refcount, exynos_drm_fb_release);
}

extern struct platform_driver fimd_driver;
extern struct platform_driver hdmi_driver;
extern struct platform_driver dp_driver;
extern struct platform_driver mixer_driver;
extern struct platform_driver exynos_drm_common_hdmi_driver;
extern struct platform_driver vidi_driver;
#ifdef CONFIG_EXYNOS_IOMMU
extern struct dma_iommu_mapping *exynos_drm_common_mapping;
#endif

/* exynos_drm_debugfs.c */
int exynos_drm_debugfs_init(struct drm_minor *minor);
void exynos_drm_debugfs_cleanup(struct drm_minor *minor);

#endif
