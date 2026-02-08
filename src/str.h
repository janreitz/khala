#ifndef STR_H
#define STR_H

#include <stddef.h>

// Temporary
#include <string>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} Str;

bool str_copy(Str* dst, const Str* src);
Str str_from_std_string(const std::string& std_string);
Str str_from_literal(const char* literal);
void str_free(Str* str);

typedef struct {
    const char* data;
    size_t len;
} StrView;

#endif