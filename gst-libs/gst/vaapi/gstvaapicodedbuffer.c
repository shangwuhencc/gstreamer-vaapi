/*
 *  gstvaapicodedbuffer.c - VA coded buffer abstraction
 *
 *  Copyright (C) 2013 Intel Corporation
 *    Author: Wind Yuan <feng.yuan@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "sysdeps.h"
#include "gstvaapicodedbuffer.h"
#include "gstvaapicodedbuffer_priv.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapiutils.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static gboolean
coded_buffer_create (GstVaapiCodedBuffer * buf, guint buf_size,
    GstVaapiContext * context)
{
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (buf);
  VABufferID buf_id;
  gboolean success;

  GST_VAAPI_DISPLAY_LOCK (display);
  success = vaapi_create_buffer (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_OBJECT_ID (context), VAEncCodedBufferType, buf_size, NULL,
      &buf_id, NULL);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!success)
    return FALSE;

  GST_DEBUG ("coded buffer %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (buf_id));
  GST_VAAPI_OBJECT_ID (buf) = buf_id;
  return TRUE;
}

static void
coded_buffer_destroy (GstVaapiCodedBuffer * buf)
{
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (buf);
  VABufferID buf_id;

  buf_id = GST_VAAPI_OBJECT_ID (buf);
  GST_DEBUG ("coded buffer %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (buf_id));

  if (buf_id != VA_INVALID_ID) {
    GST_VAAPI_DISPLAY_LOCK (display);
    vaapi_destroy_buffer (GST_VAAPI_DISPLAY_VADISPLAY (display), &buf_id);
    GST_VAAPI_DISPLAY_UNLOCK (display);
    GST_VAAPI_OBJECT_ID (buf) = VA_INVALID_ID;
  }
}

static gboolean
coded_buffer_map (GstVaapiCodedBuffer * buf)
{
  if (buf->segment_list)
    return TRUE;

  GST_VAAPI_OBJECT_LOCK_DISPLAY (buf);
  buf->segment_list = vaapi_map_buffer (GST_VAAPI_OBJECT_VADISPLAY (buf),
      GST_VAAPI_OBJECT_ID (buf));
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (buf);
  return buf->segment_list != NULL;
}

static void
coded_buffer_unmap (GstVaapiCodedBuffer * buf)
{
  if (!buf->segment_list)
    return;

  GST_VAAPI_OBJECT_LOCK_DISPLAY (buf);
  vaapi_unmap_buffer (GST_VAAPI_OBJECT_VADISPLAY (buf),
      GST_VAAPI_OBJECT_ID (buf), (void **) &buf->segment_list);
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (buf);
}

#define gst_vaapi_coded_buffer_finalize coded_buffer_destroy
GST_VAAPI_OBJECT_DEFINE_CLASS (GstVaapiCodedBuffer, gst_vaapi_coded_buffer)

/*
 * gst_vaapi_coded_buffer_new:
 * @context: the parent #GstVaapiContext object
 * @buf_size: the buffer size in bytes
 *
 * Creates a new VA coded buffer bound to the supplied @context.
 *
 * Return value: the newly allocated #GstVaapiCodedBuffer object, or
 *   %NULL if an error occurred
 */
GstVaapiCodedBuffer *
gst_vaapi_coded_buffer_new (GstVaapiContext * context, guint buf_size)
{
  GstVaapiCodedBuffer *buf;
  GstVaapiDisplay *display;

  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail (buf_size > 0, NULL);

  display = GST_VAAPI_OBJECT_DISPLAY (context);
  g_return_val_if_fail (display != NULL, NULL);

  buf = gst_vaapi_object_new (gst_vaapi_coded_buffer_class (), display);
  if (!buf)
    return NULL;

  if (!coded_buffer_create (buf, buf_size, context))
    goto error;
  return buf;

error:
  gst_vaapi_object_unref (buf);
  return NULL;
}

/*
 * gst_vaapi_coded_buffer_map:
 * @buf: a #GstVaapiCodedBuffer
 * @data: pointer to the mapped buffer data (VACodedBufferSegment)
 *
 * Maps the VA coded buffer and returns the data pointer into @data.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
gst_vaapi_coded_buffer_map (GstVaapiCodedBuffer * buf,
    VACodedBufferSegment ** out_segment_list_ptr)
{
  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (out_segment_list_ptr != NULL, FALSE);

  if (!coded_buffer_map (buf))
    return FALSE;

  *out_segment_list_ptr = buf->segment_list;
  return TRUE;
}

/*
 * gst_vaapi_coded_buffer_unmap:
 * @buf: a #GstVaapiCodedBuffer
 *
 * Unamps the VA coded buffer.
 */
void
gst_vaapi_coded_buffer_unmap (GstVaapiCodedBuffer * buf)
{
  g_return_if_fail (buf != NULL);

  coded_buffer_unmap (buf);
}

/**
 * gst_vaapi_coded_buffer_get_size:
 * @buf: a #GstVaapiCodedBuffer
 *
 * Returns the VA coded buffer size in bytes. That represents the
 * exact buffer size, as filled in so far, not the size of the
 * allocated buffer.
 *
 * Return value: the size of the VA coded buffer, or -1 on error
 */
gssize
gst_vaapi_coded_buffer_get_size (GstVaapiCodedBuffer * buf)
{
  VACodedBufferSegment *segment;
  gssize size;

  g_return_val_if_fail (buf != NULL, -1);

  if (!coded_buffer_map (buf))
    return -1;

  size = 0;
  for (segment = buf->segment_list; segment != NULL; segment = segment->next)
    size += segment->size;

  coded_buffer_unmap (buf);
  return size;
}

/**
 * gst_vaapi_coded_buffer_copy_into:
 * @dest: the destination #GstBuffer
 * @src: the source #GstVaapiCodedBuffer
 *
 * Copies the coded buffer data from @src into the regular buffer @dest.
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
gst_vaapi_coded_buffer_copy_into (GstBuffer * dest, GstVaapiCodedBuffer * src)
{
  VACodedBufferSegment *segment;
  goffset offset;
  gsize size;

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  if (!coded_buffer_map (src))
    return FALSE;

  offset = 0;
  for (segment = src->segment_list; segment != NULL; segment = segment->next) {
    size = gst_buffer_fill (dest, offset, segment->buf, segment->size);
    if (size != segment->size)
      break;
    offset += segment->size;
  }

  coded_buffer_unmap (src);
  return segment == NULL;
}
