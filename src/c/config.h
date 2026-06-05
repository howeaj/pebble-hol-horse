// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// See documentation in config.c

#pragma once

#include <pebble.h>

#include "config_defs.h"


#pragma pack(push, 1)  // prevent unpredictable format changes

#define CONFIG_STRUCT_MEMBER(_conftype, _ctype, _message_key, _default) _ctype _message_key;
typedef struct Config {
    X_CONFIG_OPTIONS(CONFIG_STRUCT_MEMBER)
} Config;
#undef CONFIG_STRUCT_MEMBER

#pragma pack(pop)

STATIC_ASSERT(sizeof(Config) <= PERSIST_DATA_MAX_LENGTH);


typedef void (*NewConfigCallback)(const Config* config);

void config_init(NewConfigCallback callback);
void config_deinit(void);
const Config* config_get(void);
