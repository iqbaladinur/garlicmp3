#ifndef DR_FLAC_CFG_H
#define DR_FLAC_CFG_H

/* Single place for dr_flac's build options; dr_flac_impl.c holds the
 * implementation, everything else includes this header only. */
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_WCHAR
#include "../third_party/dr_libs/dr_flac.h"

#endif
