// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/** App configuration via Clay (pebble package install @rebble/clay)

    To use this module, you must define the following:
        PERSIST_KEY_CONFIG_VERSION and PERSIST_KEY_CONFIG (see persist_keys.h)
        A file named config_defs.h which defines:
            X_CONFIG_OPTIONS
            PERSIST_CONFIG_VERSION

    TODO:
        Don't take over the entire app message inbox.
*/

#include "config.h"

#include <pebble.h>

#include "macros.h"
#include "persist_keys.h"

#define CONFIG_STRUCT_DEFAULT(_conftype, _ctype, _arrsuffix, _message_key, _default) ._message_key = _default,
static Config s_config = {
    X_CONFIG_OPTIONS(CONFIG_STRUCT_DEFAULT)
};
#undef CONFIG_STRUCT_DEFAULT

static NewConfigCallback s_new_config_callback = NULL;


/******************************************************************************
 Private methods; Local watch persistence
******************************************************************************/

static bool is_local_persist_written_and_current_version(void) {
    return persist_read_int(PERSIST_KEY_CONFIG_VERSION) == PERSIST_CONFIG_VERSION;
}

static void local_persist_load(void) {
    StatusCode status = E_DOES_NOT_EXIST;
    if (is_local_persist_written_and_current_version()){
        status = persist_read_data(PERSIST_KEY_CONFIG, &s_config, sizeof(s_config));
        LOG("Loaded config from persistent storage");
    }
    if (status <= 0) {
        LOG("Config not loaded from persistent storage (%d)", status);
    }
}

static void local_persist_save(void) {
    StatusCode status = persist_write_data(PERSIST_KEY_CONFIG, &s_config, sizeof(s_config));
    ASSERT(status == sizeof(s_config));

    if (status == sizeof(s_config)) {
        status = persist_write_int(PERSIST_KEY_CONFIG_VERSION, PERSIST_CONFIG_VERSION);
        ASSERT(status == sizeof(int32_t));
    }
}


/******************************************************************************
 Private methods; Receive config from phone
******************************************************************************/

#define RECEIVE_CONFIG(message_key, convert, tuple_type, _length) MACRO_START \
    const Tuple *tuple = dict_find(iter, MESSAGE_KEY_##message_key); \
    if (tuple) { \
        if ((tuple->type == tuple_type) && (tuple->length == _length)) { \
            s_config.message_key = convert; \
        } else { \
            LOG_ERR("ERR %u %u: " #message_key, tuple->type, tuple->length); \
        } \
    } else { \
        LOG("MISSING: " #message_key); \
    } \
MACRO_END

// Clay sends basic values as int32
#define RECEIVE_CONFIG_INT32(message_key, convert) RECEIVE_CONFIG(message_key, convert, TUPLE_INT, 4)
#define RECEIVE_CONFIG_INT(message_key) RECEIVE_CONFIG_INT32(message_key, tuple->value->int32)
#define RECEIVE_CONFIG_BOOL(message_key) RECEIVE_CONFIG_INT32(message_key, (tuple->value->int32 == 1))
#define RECEIVE_CONFIG_COLOR(message_key) RECEIVE_CONFIG_INT32(message_key, GColorFromHEX(tuple->value->int32))

// A hack to work around https://github.com/pebble-dev/clay/issues/28. Interprets a single numeric character as int.
#define RECEIVE_CONFIG_CHAR_AS_INT(message_key, zero_char) \
    RECEIVE_CONFIG(message_key, (((char*)tuple->value->cstring)[0] - zero_char), TUPLE_CSTRING, 2)
#define RECEIVE_CONFIG_ENUM(message_key) RECEIVE_CONFIG_CHAR_AS_INT(message_key, '0')

// Receive an int32 or cstring as a char
#define RECEIVE_CONFIG_CHAR(message_key) MACRO_START \
    const Tuple *tuple = dict_find(iter, MESSAGE_KEY_##message_key); \
    if (tuple) { \
        if ((tuple->type == TUPLE_INT) \
                && (tuple->length == 4) \
                && WITHIN(tuple->value->int32, INT8_MIN, INT8_MAX)) { \
            s_config.message_key = (int8_t)tuple->value->int32; \
        } else if ((tuple->type == TUPLE_CSTRING) \
                && (tuple->length == 2)) { \
            s_config.message_key = ((char*)tuple->value->cstring)[0]; \
        } else { \
            LOG("ERR %u %u %d: " #message_key, tuple->type, tuple->length, tuple->value->int32); \
        } \
    } else { \
        LOG("MISSING: " #message_key); \
    } \
MACRO_END

#define RECEIVE_CONFIG_STR(message_key) MACRO_START \
    const Tuple *tuple = dict_find(iter, MESSAGE_KEY_##message_key); \
    if (tuple) { \
        if (tuple->type == TUPLE_CSTRING) { \
            /* Note pebble's implementation of strncpy is non-standard and *does* ensure null-termination. */ \
            strncpy(s_config.message_key, tuple->value->cstring, sizeof(s_config.message_key)); \
        } else { \
            LOG_ERR("ERR %d: " #message_key, tuple->type); \
        } \
    } else { \
        LOG("MISSING: " #message_key); \
    } \
MACRO_END

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
    Config saved_config = s_config;

#define CONFIG_STRUCT_RECEIVE(_conftype, _ctype, _arrsuffix, _message_key, _default) RECEIVE_CONFIG_##_conftype(_message_key);
    X_CONFIG_OPTIONS(CONFIG_STRUCT_RECEIVE)
#undef CONFIG_STRUCT_RECEIVE

    if (memcmp(&saved_config, &s_config, sizeof(saved_config)) != 0) {
        LOG("New app config received");
        local_persist_save();
        if (s_new_config_callback != NULL) {
            s_new_config_callback(&s_config);
        }
    }
}


/******************************************************************************
 Public methods
******************************************************************************/

// `callback` is an optional function to call whenever a new config is received from the phone;
// it should be used to mark affected layers dirty or otherwise live-update config changes
void config_init(NewConfigCallback callback) {
    s_new_config_callback = callback;
    local_persist_load();
    app_message_register_inbox_received(&inbox_received_handler);

    AppMessageResult result = app_message_open(256, 0);
    if (result != APP_MSG_OK) {
        LOG("app_message_open failed: 0x%x", result);
    }
}

void config_deinit(void) {
    app_message_deregister_callbacks();
}

const Config* config_get(void) {
    return &s_config;
}
