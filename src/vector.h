
#ifndef VECTOR_H
#define VECTOR_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Vec {
    void* data;
    size_t count;
    size_t capacity;
    size_t element_size;
} Vec;

void vec_init(Vec*, size_t);
// Reset count, keep buffer
void vec_clear(Vec *v);
void vec_free(Vec *v);
int vec_push(Vec*, const void*);
void vec_pop(Vec*, void*);
void* vec_at_mut(Vec*, size_t);
const void* vec_at(const Vec*, size_t);
void vec_cp_at(const Vec*, size_t, void*);
int vec_reserve(Vec*, size_t);

typedef bool (*ElementCallback)(const void* element, void* user_data);
void vec_for_each(const Vec* v, ElementCallback cb, void* user_data);

typedef bool (*ElementCallbackMut)(void* element, void* user_data);
void vec_for_each_mut(Vec* v, ElementCallbackMut cb, void* user_data);

typedef bool (*VecFindIfCallback)(const void* element, const void* user_data);
const void* vec_find_if(const Vec* v, VecFindIfCallback cb, const void* user_data);

// Collect items into a Vec, to be used as callback in X_for_each functions
// You need to ensure that vec and elements have compatible types
bool vec_collect_elements(const void* element, void* vec);

#ifdef __cplusplus
}
#endif

#endif