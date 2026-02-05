#include "vector.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void vec_init(Vec* vec, size_t element_size) {
    vec->data = NULL;
    vec->capacity = 0;
    vec->count = 0;
    vec->element_size = element_size;
}

void vec_clear(Vec *v) {
    v->count = 0;  // keep capacity, reuse buffer
}

void vec_free(Vec *v) {
    free(v->data);
    v->data = NULL;
    v->count = 0;
    v->capacity = 0;
}

int vec_push(Vec* vec, const void* element) {
    assert (vec->count <= vec->capacity);
    if (vec->count == vec->capacity) {
        const size_t new_cap = vec->capacity ? vec->capacity * 2 : 8;
        const int reserve_ok = vec_reserve(vec, new_cap);
        if (reserve_ok != 0) { return reserve_ok; }
    }
    memcpy((char*)vec->data + (vec->count * vec->element_size), element, vec->element_size);
    vec->count++;
    return 0;
}

void vec_pop(Vec* vec, void* out_element) {
    assert(vec->count > 0);
    vec->count--;
    memcpy(out_element, (char*)vec->data + (vec->count * vec->element_size), vec->element_size);
}

void* vec_at_mut(Vec* vec, size_t idx) {
    assert(idx < vec->count);
    return (char*)vec->data + (idx * vec->element_size);
}

const void* vec_at(const Vec* vec, size_t idx) {
    assert(idx < vec->count);
    return (char*)vec->data + (idx * vec->element_size);
}

void vec_cp_at(const Vec* vec, size_t idx, void* out_element) {
    assert(idx < vec->count);
    memcpy(out_element, (char*)vec->data + (idx * vec->element_size), vec->element_size);
}

int vec_reserve(Vec* vec, size_t count) {
    if (vec->capacity < count) {
        void* new_data = realloc(vec->data, count * vec->element_size);
        if (new_data == NULL) { return -1; }
        vec->data = new_data;
        vec->capacity = count;
    }
    return 0;
}