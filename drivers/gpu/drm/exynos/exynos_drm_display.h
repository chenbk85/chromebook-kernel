/*
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _EXYNOS_DRM_PANEL_H_
#define _EXYNOS_DRM_PANEL_H_

struct exynos_drm_overlay;

/*
 * Callbacks used to manipulate the panel (DP/HDMI/MIPI)
 *
 * @subdrv_probe: Used to associate drm_dev with panel context
 * @is_connected: Returns true if the panel is connected
 * @get_edid: Returns an edid with mode data from the panel
 * @check_timing: Returns 0 if the given timing is valid for the panel
 * @dpms: Sets the panel's power to mode and enables/disables video output
 * @mode_fixup: Copies and optionally alters mode to adjusted_mode
 * @mode_set: Sets the panel to output mode
 * @get_max_res: Returns the maximum resolution in width/height
 */
struct exynos_panel_ops {
	int (*subdrv_probe)(void *ctx, struct drm_device *drm_dev);
	bool (*is_connected)(void *ctx);
	struct edid *(*get_edid)(void *ctx, struct drm_connector *connector);
	int (*check_timing)(void *ctx, void *timing);
	int (*dpms)(void *ctx, int mode);
	void (*mode_fixup)(void *ctx, struct drm_connector *connector,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode);
	void (*mode_set)(void *ctx, struct drm_display_mode *mode);
	void (*get_max_res)(void *ctx, unsigned int *width,
				unsigned int *height);
};

/*
 * Callbacks used to manipulate the controller (FIMD/Mixer)
 *
 * @subdrv_probe: Used to associate drm_dev with the controller context
 * @get_panel: If we're not using edid, return the panel info
 * @enable_vblank: Enable the controller's vblank interrupt and set pipe
 * @disable_vblank: Disable the controller's vblank interrupt
 * @dpms: Sets the controller's power to mode
 * @mode_set: Sets the controller to output mode
 * @commit: Applies controller level settings (as opposed to window level)
 * @win_commit: Commits the changes on only one window
 * @win_disable: Disables one of the controller's windows
 * @adjust_modes: Allows the controller to rework the connector modes
 */
struct exynos_controller_ops {
	int (*subdrv_probe)(void *ctx, struct drm_device *drm_dev);
	struct exynos_drm_panel_info *(*get_panel)(void *ctx);
	int (*enable_vblank)(void *ctx, int pipe);
	void (*disable_vblank)(void *ctx);
	int (*dpms)(void *ctx, int mode);
	void (*mode_set)(void *ctx, struct exynos_drm_overlay *overlay);
	void (*commit)(void *ctx);
	void (*win_commit)(void *ctx, int zpos);
	void (*win_disable)(void *ctx, int zpos);
	void (*adjust_modes)(void *ctx, struct drm_connector *connector);
};

/*
 * The various bits we need to keep track of to manipulate the hardware from
 * the connector/encoder/crtc drm callbacks.
 *
 * @display_type: The type of display, depends on which CONFIGs are enabled
 * @subdrv: The exynos drm sub driver pointer
 * @panel_ops: The panel callbacks to use
 * @controller_ops: The controller callbacks to use
 * @panel_ctx: The context pointer to pass to panel callbacks
 * @controller_ctx: The context pointer to pass to controller callbacks
 * @pipe: The current pipe number for this display
 * @suspend_dpms: The dpms mode of the display before suspend
 */
struct exynos_drm_display {
	enum exynos_drm_display_type display_type;
	struct exynos_drm_subdrv *subdrv;
	struct exynos_panel_ops *panel_ops;
	struct exynos_controller_ops *controller_ops;
	void *panel_ctx;
	void *controller_ctx;
	int pipe;
	int suspend_dpms;
};

const char *exynos_display_type_name(const struct exynos_drm_display *display);

/*
 * Used by the hardware drivers to attach panel and controller callbacks and
 * contexts to a display.
 */
void exynos_display_attach_panel(enum exynos_drm_display_type type,
		struct exynos_panel_ops *ops, void *ctx);
void exynos_display_attach_controller(enum exynos_drm_display_type type,
		struct exynos_controller_ops *ops, void *ctx);

/* Initializes the given display to type */
int exynos_display_init(struct exynos_drm_display *display,
		enum exynos_drm_display_type type);

/* Cleans up the given display */
void exynos_display_remove(struct exynos_drm_display *display);

#endif
