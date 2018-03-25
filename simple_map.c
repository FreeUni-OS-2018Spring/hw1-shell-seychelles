#include "simple_map.h"

struct key_value {
    char* key;
    char* value;
};

static void free_elem(void *elem) {
  struct key_value* kv = (struct key_value*)elem;
  free(kv->key);
  free(kv->value);
}

void simple_map_new(simple_map* m) {
    VectorNew(&m->storage, sizeof(struct key_value), free_elem, 4);
}

void simple_map_put(simple_map* m, char* key, char* value) {
    int size = VectorLength(&m->storage);
    for (int i = 0; i < size; i++) {
        struct key_value* kv = VectorNth(&m->storage, i);
        if (strcmp(kv->key, key) == 0) {
            free(kv->value);
            kv->value = value;
            return;
        }
    }
    struct key_value kv;
    kv.key = key;
    kv.value = value;
    VectorAppend(&m->storage, &kv);
}

char* simple_map_get(simple_map* m, char* key) {
    int size = VectorLength(&m->storage);
    for (int i = 0; i < size; i++) {
        struct key_value* kv = VectorNth(&m->storage, i);
        if (strcmp(kv->key, key) == 0) return kv->value;
    }
    return NULL;
}

int simple_map_size(simple_map* m) {
    int size = VectorLength(&m->storage);
    return size;
}

void simple_map_dispose(simple_map* m) {
    VectorDispose(&m->storage);
}

