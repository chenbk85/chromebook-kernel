/* exynos_drm_fbdev.c
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

#include "drmP.h"
#include "drm_crtc.h"
#include "drm_fb_helper.h"
#include "drm_crtc_helper.h"

#include "exynos_drm_drv.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_gem.h"

#define MAX_CONNECTOR		4
#define PREFERRED_BPP		32

#define to_exynos_fbdev(x)	container_of(x, struct exynos_drm_fbdev,\
				drm_fb_helper)

struct exynos_drm_fbdev {
	struct drm_fb_helper		drm_fb_helper;
	struct exynos_drm_gem_obj	*exynos_gem_obj;
};

static int
exynos_drm_fb_mmap(struct fb_info *info, struct vm_area_struct * vma)
{
	int ret;

	DRM_DEBUG_KMS("pgoff: 0x%lx vma: 0x%lx - 0x%lx (0x%lx)\n",
			vma->vm_pgoff, vma->vm_start, vma->vm_end,
			vma->vm_end - vma->vm_start);

	DRM_DEBUG_KMS("screen: 0x%p (0x%lx) smem: 0x%lx (0x%x)\n",
			info->screen_base, info->screen_size,
			info->fix.smem_start, info->fix.smem_len);

	vma->vm_pgoff = 0;
	ret = dma_mmap_writecombine(info->device, vma, info->screen_base,
			info->fix.smem_start, vma->vm_end - vma->vm_start);
	if (ret)
		printk(KERN_ERR "Remapping memory failed, error: %d\n", ret);

	return ret;
}

static struct fb_ops exynos_drm_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_mmap	= exynos_drm_fb_mmap,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_check_var	= drm_fb_helper_check_var,
	.fb_set_par	= drm_fb_helper_set_par,
	.fb_blank	= drm_fb_helper_blank,
	.fb_pan_display	= drm_fb_helper_pan_display,
	.fb_setcmap	= drm_fb_helper_setcmap,
};

static void exynos_drm_fbdev_update(struct drm_fb_helper *helper,
				    struct drm_framebuffer *fb,
				    struct exynos_drm_gem_obj *exynos_gem_obj,
				    unsigned int fb_width,
				    unsigned int fb_height)
{
	struct fb_info *fbi = helper->fbdev;
	struct drm_device *dev = helper->dev;
	struct exynos_drm_gem_buf *buffer = exynos_gem_obj->buffer;
	struct drm_gem_object *drm_gem_object = &exynos_gem_obj->base;
	size_t size = drm_gem_object->size;

	DRM_DEBUG_KMS("[FB:%d] [NEW FB:%d]\n", DRM_BASE_ID(helper->fb),
			DRM_BASE_ID(fb));

	drm_fb_helper_fill_fix(fbi, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(fbi, helper, fb_width, fb_height);

	dev->mode_config.fb_base = (resource_size_t)buffer->dma_addr;
	fbi->screen_base = buffer->kvaddr;
	fbi->fix.smem_start = (unsigned long)(buffer->dma_addr);
	fbi->screen_size = size;
	fbi->fix.smem_len = size;
}

static int exynos_drm_fbdev_create(struct drm_fb_helper *helper,
				    struct drm_fb_helper_surface_size *sizes)
{
	struct exynos_drm_fbdev *exynos_fbdev = to_exynos_fbdev(helper);
	struct exynos_drm_gem_obj *exynos_gem_obj;
	struct exynos_drm_fb *exynos_fb;
	struct drm_device *dev = helper->dev;
	struct fb_info *fbi;
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct platform_device *pdev = dev->platformdev;
	unsigned long size;
	int ret;

	DRM_DEBUG_KMS("[FB:%d]: %dx%d surface: %dx%d bpp: %d\n",
			DRM_BASE_ID(helper->fb),
			sizes->fb_width, sizes->fb_height,
			sizes->surface_width, sizes->surface_height,
			sizes->surface_bpp);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = sizes->surface_width * (sizes->surface_bpp >> 3);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	mutex_lock(&dev->struct_mutex);

	fbi = framebuffer_alloc(0, &pdev->dev);
	if (!fbi) {
		DRM_ERROR("failed to allocate fb info.\n");
		ret = -ENOMEM;
		goto out;
	}

	size = mode_cmd.pitches[0] * mode_cmd.height;

	/* 0 means to allocate physically continuous memory */
	exynos_gem_obj = exynos_drm_gem_create(dev, 0, size);
	if (IS_ERR(exynos_gem_obj)) {
		ret = PTR_ERR(exynos_gem_obj);
		goto out;
	}

	exynos_fbdev->exynos_gem_obj = exynos_gem_obj;

	exynos_fb = exynos_drm_fb_init(dev, &mode_cmd);
	if (IS_ERR_OR_NULL(exynos_fb)) {
		DRM_ERROR("failed to create drm framebuffer.\n");
		ret = PTR_ERR(exynos_fb);
		goto out;
	}
	exynos_fb->exynos_gem_obj[0] = exynos_gem_obj;

	helper->fb = &exynos_fb->fb;
	helper->fbdev = fbi;

	fbi->par = helper;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &exynos_drm_fb_ops;

	ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (ret) {
		DRM_ERROR("failed to allocate cmap.\n");
		goto out;
	}

	/* RGB formats use only one buffer */
	exynos_drm_fbdev_update(helper, helper->fb, exynos_gem_obj,
			sizes->fb_width, sizes->fb_height);

/*
 * if failed, all resources allocated above would be released by
 * drm_mode_config_cleanup() when drm_load() had been called prior
 * to any specific driver such as fimd or hdmi driver.
 */
out:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static int exynos_drm_fbdev_probe(struct drm_fb_helper *helper,
				   struct drm_fb_helper_surface_size *sizes)
{
	int ret = 0;

	DRM_DEBUG_KMS("[FB:%d]: %dx%d surface: %dx%d bpp: %d\n",
			DRM_BASE_ID(helper->fb),
			sizes->fb_width, sizes->fb_height,
			sizes->surface_width, sizes->surface_height,
			sizes->surface_bpp);

	/*
	 * with !helper->fb, it means that this function is called first time
	 * and after that, the helper->fb would be used as clone mode.
	 */
	if (!helper->fb) {
		ret = exynos_drm_fbdev_create(helper, sizes);
		if (ret < 0) {
			DRM_ERROR("failed to create fbdev.\n");
			return ret;
		}

		/*
		 * fb_helper expects a value more than 1 if succeed
		 * because register_framebuffer() should be called.
		 */
		ret = 1;
	}

	return ret;
}

static struct drm_fb_helper_funcs exynos_drm_fb_helper_funcs = {
	.fb_probe =	exynos_drm_fbdev_probe,
};

int exynos_drm_fbdev_init(struct drm_device *dev)
{
	struct exynos_drm_fbdev *fbdev;
	struct exynos_drm_private *private = dev->dev_private;
	struct drm_fb_helper *helper;
	unsigned int num_crtc;
	int ret;

	DRM_DEBUG_KMS("[DEV:%s]\n", dev->devname);

	if (!dev->mode_config.num_crtc || !dev->mode_config.num_connector)
		return 0;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev) {
		DRM_ERROR("failed to allocate drm fbdev.\n");
		return -ENOMEM;
	}

	private->fb_helper = helper = &fbdev->drm_fb_helper;
	helper->funcs = &exynos_drm_fb_helper_funcs;

	num_crtc = dev->mode_config.num_crtc;

	ret = drm_fb_helper_init(dev, helper, num_crtc, MAX_CONNECTOR);
	if (ret < 0) {
		DRM_ERROR("failed to initialize drm fb helper.\n");
		goto err_init;
	}

	ret = drm_fb_helper_single_add_all_connectors(helper);
	if (ret < 0) {
		DRM_ERROR("failed to register drm_fb_helper_connector.\n");
		goto err_setup;

	}

	ret = drm_fb_helper_initial_config(helper, PREFERRED_BPP);
	if (ret < 0) {
		DRM_ERROR("failed to set up hw configuration.\n");
		goto err_setup;
	}

	return 0;

err_setup:
	drm_fb_helper_fini(helper);

err_init:
	private->fb_helper = NULL;
	kfree(fbdev);

	return ret;
}

static void exynos_drm_fbdev_destroy(struct drm_device *dev,
				      struct drm_fb_helper *fb_helper)
{
	struct drm_framebuffer *fb;

	DRM_DEBUG_KMS("[DEV:%s] [FB:%d]\n", dev->devname,
			fb_helper ? DRM_BASE_ID(fb_helper->fb) : -1);

	/* release drm framebuffer and real buffer */
	if (fb_helper->fb && fb_helper->fb->funcs) {
		fb = fb_helper->fb;
		if (fb && fb->funcs->destroy)
			fb->funcs->destroy(fb);
	}

	/* release linux framebuffer */
	if (fb_helper->fbdev) {
		struct fb_info *info;
		int ret;

		info = fb_helper->fbdev;
		ret = unregister_framebuffer(info);
		if (ret < 0)
			DRM_DEBUG_KMS("failed unregister_framebuffer()\n");

		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);

		framebuffer_release(info);
	}

	drm_fb_helper_fini(fb_helper);
}

void exynos_drm_fbdev_fini(struct drm_device *dev)
{
	struct exynos_drm_private *private = dev->dev_private;
	struct exynos_drm_fbdev *fbdev;

	DRM_DEBUG_KMS("[DEV:%s]\n", dev->devname);

	if (!private || !private->fb_helper)
		return;

	fbdev = to_exynos_fbdev(private->fb_helper);

	if (fbdev->exynos_gem_obj)
		exynos_drm_gem_destroy(fbdev->exynos_gem_obj);

	exynos_drm_fbdev_destroy(dev, private->fb_helper);
	kfree(fbdev);
	private->fb_helper = NULL;
}

void exynos_drm_fbdev_restore_mode(struct drm_device *dev)
{
	struct exynos_drm_private *private = dev->dev_private;

	DRM_DEBUG_KMS("[DEV:%s]\n", dev->devname);

	if (!private || !private->fb_helper)
		return;

	drm_fb_helper_restore_fbdev_mode(private->fb_helper);
}
