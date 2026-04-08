/* GStreamer Wayland Library
 *
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2014 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#pragma once

#include <gst/wayland/wayland.h>

#include <gst/video/video.h>

#include <drm_fourcc.h>

G_BEGIN_DECLS

#ifndef DRM_FORMAT_NV15
#define DRM_FORMAT_NV15 fourcc_code('N', 'V', '1', '5')
#endif

#ifndef DRM_FORMAT_NV20
#define DRM_FORMAT_NV20 fourcc_code('N', 'V', '2', '0')
#endif

#ifndef DRM_FORMAT_YUV420_8BIT
#define DRM_FORMAT_YUV420_8BIT fourcc_code('Y', 'U', '0', '8')
#endif

#ifndef DRM_FORMAT_YUV420_10BIT
#define DRM_FORMAT_YUV420_10BIT fourcc_code('Y', 'U', '1', '0')
#endif

#ifndef DRM_FORMAT_MOD_VENDOR_ARM
#define DRM_FORMAT_MOD_VENDOR_ARM 0x08
#endif

#ifndef DRM_FORMAT_MOD_ARM_AFBC
#define DRM_FORMAT_MOD_ARM_AFBC(__afbc_mode) fourcc_mod_code(ARM, __afbc_mode)
#endif

#ifndef AFBC_FORMAT_MOD_BLOCK_SIZE_16x16
#define AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 (1ULL)
#endif

#ifndef AFBC_FORMAT_MOD_SPARSE
#define AFBC_FORMAT_MOD_SPARSE (((__u64)1) << 6)
#endif

#define DRM_AFBC_MODIFIER \
  (DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_SPARSE) | \
   DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16))

#ifndef GST_VIDEO_FLAG_ARM_AFBC
#define GST_VIDEO_FLAG_ARM_AFBC (1UL << 31)
#define GST_VIDEO_INFO_SET_AFBC(i) \
  GST_VIDEO_INFO_FLAG_SET (i, GST_VIDEO_FLAG_ARM_AFBC)
#define GST_VIDEO_INFO_UNSET_AFBC(i) \
  GST_VIDEO_INFO_FLAG_UNSET (i, GST_VIDEO_FLAG_ARM_AFBC)
#define GST_VIDEO_INFO_IS_AFBC(i) \
  GST_VIDEO_INFO_FLAG_IS_SET (i, GST_VIDEO_FLAG_ARM_AFBC)
#endif

GST_WL_API
void gst_wl_videoformat_init_once (void);

GST_WL_API
enum wl_shm_format gst_video_format_to_wl_shm_format (GstVideoFormat format);

GST_WL_API
gint gst_video_format_to_wl_dmabuf_format (GstVideoFormat format);

GST_WL_API
GstVideoFormat gst_wl_shm_format_to_video_format (enum wl_shm_format wl_format);

GST_WL_API
GstVideoFormat gst_wl_dmabuf_format_to_video_format (guint wl_format);

GST_WL_API
const gchar *gst_wl_shm_format_to_string (enum wl_shm_format wl_format);

GST_WL_API
const gchar *gst_wl_dmabuf_format_to_string (guint wl_format);

G_END_DECLS
