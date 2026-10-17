/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

/// Copied from PebbleOS\fw\applib\graphics\bitblt_private.h

#pragma once

#include <pebble.h>

void bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit(GBitmap *dest_bitmap, const GBitmap *src_bitmap,
                                                  GRect dest_rect, GPoint src_origin_offset);