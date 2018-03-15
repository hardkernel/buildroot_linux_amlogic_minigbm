/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef DRV_AMLOGIC

#include <errno.h>
#include <meson_drm.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <inttypes.h>

#include "drv_priv.h"
#include "helpers.h"
#include "util.h"


static const uint32_t render_target_formats[] = { DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888 };

static int amlogic_init(struct driver *drv)
{
	drv_add_combinations(drv, render_target_formats, ARRAY_SIZE(render_target_formats),
			      &LINEAR_METADATA, BO_USE_RENDER_MASK);

	return drv_modify_linear_combinations(drv);
}

static bool has_modifier(const uint64_t *list, uint32_t count, uint64_t modifier)
{
	uint32_t i;
	for (i = 0; i < count; i++)
		if (list[i] == modifier)
			return true;

	return false;
}

static int amlogic_bo_create_with_modifiers(struct bo *bo, uint32_t width, uint32_t height,
					     uint32_t format, const uint64_t *modifiers,
					     uint32_t count)
{
	int ret;
	size_t plane;
	struct drm_meson_gem_create gem_create;

	if (format == DRM_FORMAT_NV12) {
		uint32_t w_mbs = DIV_ROUND_UP(ALIGN(width, 16), 16);
		uint32_t h_mbs = DIV_ROUND_UP(ALIGN(height, 16), 16);

		uint32_t aligned_width = w_mbs * 16;
		uint32_t aligned_height = DIV_ROUND_UP(h_mbs * 16 * 3, 2);

		drv_bo_from_format(bo, aligned_width, height, format);
		bo->total_size = bo->strides[0] * aligned_height + w_mbs * h_mbs * 128;
	} else {
		if (!has_modifier(modifiers, count, DRM_FORMAT_MOD_LINEAR)) {
			errno = EINVAL;
			fprintf(stderr, "no usable modifier found\n");
			return -1;
		}

		uint32_t stride;
		/*
		 * Since the ARM L1 cache line size is 64 bytes, align to that
		 * as a performance optimization. For YV12, the Mali cmem allocator
		 * requires that chroma planes are aligned to 64-bytes, so align the
		 * luma plane to 128 bytes.
		 */
		stride = drv_stride_from_format(format, width, 0);
		if (format == DRM_FORMAT_YVU420 || format == DRM_FORMAT_YVU420_ANDROID)
			stride = ALIGN(stride, 128);
		else
			stride = ALIGN(stride, 64);

		drv_bo_from_format(bo, stride, height, format);
	}

	memset(&gem_create, 0, sizeof(gem_create));
	gem_create.size = bo->total_size;
	gem_create.flags = bo->use_flags;

	ret = drmIoctl(bo->drv->fd, DRM_IOCTL_MESON_GEM_CREATE, &gem_create);

	if (ret) {
		fprintf(stderr, "drv: DRM_IOCTL_MESON_GEM_CREATE failed (size=%"PRIu64")\n",
			gem_create.size);
		return ret;
	}

	for (plane = 0; plane < bo->num_planes; plane++)
		bo->handles[plane].u32 = gem_create.handle;

	return 0;
}

static int amlogic_bo_create(struct bo *bo, uint32_t width, uint32_t height, uint32_t format,
			      uint64_t use_flags)
{
	uint64_t modifiers[] = { DRM_FORMAT_MOD_LINEAR };

	return amlogic_bo_create_with_modifiers(bo, width, height, format, modifiers,
						 ARRAY_SIZE(modifiers));
}

const struct backend backend_aml = {
	.name = "meson",
	.init = amlogic_init,

	.bo_create = amlogic_bo_create,
	.bo_create_with_modifiers = amlogic_bo_create_with_modifiers,
	.bo_destroy = drv_dumb_bo_destroy,
	.bo_import = drv_prime_bo_import,
	.bo_map = drv_dumb_bo_map,
	.bo_unmap = drv_bo_munmap,
};

#endif
