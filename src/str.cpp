#include "str.h"

#include <cstring>
#include <stdlib.h>

bool str_copy(Str* dst, const Str* src)
{
    auto new_data = (char*)malloc(src->len + 1);
    if (new_data == NULL) return false;
    memcpy(new_data, src->data, src->len);
    new_data[src->len] = '\0';

    free(dst->data);
    dst->data = new_data;
    dst->len = src->len;
    dst->cap = src->len + 1;

    return true;
}

Str str_from_std_string(const std::string &std_string)
{
    Str str;
    str.len = std_string.size();
    str.cap = str.len + 1;
    str.data = static_cast<char *>(malloc(str.cap));
    if (str.data) {
        memcpy(str.data, std_string.data(), std_string.size());
        str.data[str.len] = '\0';
    }
    return str;
}

Str str_from_literal(const char *literal)
{
    Str str;
    str.len = strlen(literal);
    str.cap = str.len + 1;
    str.data = static_cast<char *>(malloc(str.cap));
    if (str.data) {
        memcpy(str.data, literal, str.len);
        str.data[str.len] = '\0';
    }
    return str;
}

void str_free(Str *str)
{
    str->len = 0;
    str->cap = 0;
    free(str->data);
    str->data = NULL;
}