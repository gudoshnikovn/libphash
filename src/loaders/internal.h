#ifndef PH_LOADERS_INTERNAL_H
#define PH_LOADERS_INTERNAL_H

#include "loader.h"

#ifdef PH_USE_TURBOJPEG
int ph_can_read_jpeg(const uint8_t *magic, size_t len);
#endif

#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
int ph_can_read_png(const uint8_t *magic, size_t len);
#endif

#ifdef PH_USE_WEBP
int ph_can_read_webp(const uint8_t *magic, size_t len);
#endif

#endif // PH_LOADERS_INTERNAL_H
