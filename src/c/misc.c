// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#include "misc.h"

#include <pebble.h>


// in-place string to upper case
void string_to_upper(char* str, size_t n) {
    for (size_t i = 0; (i < n) && str[i]; i++){
        str[i] = toupper(str[i]);
    }
}

// in-place string to lower case
void string_to_lower(char* str, size_t n) {
    for (size_t i = 0; (i < n) && str[i]; i++){
        str[i] = tolower(str[i]);
    }
}

// safe string copy
ssize_t strscpy(char * restrict dst, const char * restrict src, size_t size) {
    if (size == 0) {
        return -1;
    }

    size_t i = 0;
    while ((src[i] != '\0') && (i < (size - 1)))
    {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';

    if (src[i] == '\0') {
        return i;
    } else {
        return -1;
    }
}
