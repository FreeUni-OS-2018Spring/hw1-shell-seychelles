#pragma once
#include "vector.h"

/* Works only for C strings, uses vector.
 * Strings must be allocated in heap or seg fault will occur.
 */
typedef struct {
    vector storage;
} simple_map;

void simple_map_new(simple_map* m);

void simple_map_put(simple_map* m, char* key, char* value);

char* simple_map_get(simple_map* m, char* key);

void simple_map_dispose(simple_map* m);

int simple_map_size(simple_map* m);
