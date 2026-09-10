#ifndef UNITY_H
#define UNITY_H

#include <cute.h>
#include <SDL3/SDL.h>

#include "common/types.h"
#include "common/memory.h"
#include "common/utility.h"
#include "common/config.h"
#include "common/assets.h"
#include "common/platform.h"
#include "app/app.h"
#include "app/ui.h"

#define DDS_LOADER_IMPLEMENTATION
#define DDS_ALLOC cf_alloc
#define DDS_FREE cf_free
#include "sfh/dds_loader.h"

// -----------------------------

#include "common/memory.c"
#include "common/config.c"
#include "common/assets.c"
#include "common/utility.c"

#ifdef _WIN32
#include "common/platform_windows.c"
#endif

#include "app/app.c"
#include "app/ui.c"

#endif //UNITY_H
