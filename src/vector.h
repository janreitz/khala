
#ifndef VECTOR_H
#define VECTOR_H

#include <stddef.h>

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
int vec_push(Vec*, const void*);
void vec_pop(Vec*, void*);
void vec_at(Vec*, size_t, void*);
int vec_reserve(Vec*, size_t);

#ifdef __cplusplus
}
#endif

#endif