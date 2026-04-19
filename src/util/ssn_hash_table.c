/*
 * SSN Hash Table Implementation
 */

#include "ssn_hash_table.h"
#include <stdlib.h>
#include <string.h>

#define SSN_HASH_TABLE_LOAD_FACTOR 0.75

typedef struct hash_node {
    uintptr_t key;
    void* value;
    struct hash_node* next;
} hash_node_t;

struct ssn_hash_table {
    hash_node_t** buckets;
    size_t capacity;
    size_t size;
};

static size_t default_capacity(size_t capacity)
{
    size_t c = 16;
    while (c < capacity) {
        c *= 2;
    }
    return c;
}

static uint32_t hash_uint32(uint32_t key)
{
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = (key >> 16) ^ key;
    return key;
}

ssn_hash_table_t* ssn_hash_table_create(size_t capacity)
{
    ssn_hash_table_t* table = (ssn_hash_table_t*)malloc(
        sizeof(ssn_hash_table_t));
    if (!table) {
        return NULL;
    }

    table->capacity = default_capacity(capacity);
    table->buckets = (hash_node_t**)calloc(table->capacity,
                                           sizeof(hash_node_t*));
    if (!table->buckets) {
        free(table);
        return NULL;
    }

    table->size = 0;

    return table;
}

void ssn_hash_table_destroy(ssn_hash_table_t* table)
{
    if (!table) {
        return;
    }

    for (size_t i = 0; i < table->capacity; i++) {
        hash_node_t* node = table->buckets[i];
        while (node) {
            hash_node_t* next = node->next;
            free(node);
            node = next;
        }
    }

    free(table->buckets);
    free(table);
}

bool ssn_hash_table_set(ssn_hash_table_t* table, void* key, void* value)
{
    if (!table || !key) {
        return false;
    }

    if ((float)table->size / table->capacity > SSN_HASH_TABLE_LOAD_FACTOR) {
        size_t new_capacity = table->capacity * 2;
        hash_node_t** new_buckets = (hash_node_t**)calloc(
            new_capacity, sizeof(hash_node_t*));
        if (!new_buckets) {
            return false;
        }

        for (size_t i = 0; i < table->capacity; i++) {
            hash_node_t* node = table->buckets[i];
            while (node) {
                hash_node_t* next = node->next;
                uint32_t hash = hash_uint32((uint32_t)node->key);
                size_t index = hash % new_capacity;
                node->next = new_buckets[index];
                new_buckets[index] = node;
                node = next;
            }
        }

        free(table->buckets);
        table->buckets = new_buckets;
        table->capacity = new_capacity;
    }

    uintptr_t int_key = (uintptr_t)key;
    uint32_t hash = hash_uint32((uint32_t)int_key);
    size_t index = hash % table->capacity;
    hash_node_t* node = table->buckets[index];

    while (node) {
        if (node->key == int_key) {
            node->value = value;
            return true;
        }
        node = node->next;
    }

    hash_node_t* new_node = (hash_node_t*)malloc(sizeof(hash_node_t));
    if (!new_node) {
        return false;
    }

    new_node->key = int_key;
    new_node->value = value;
    new_node->next = table->buckets[index];
    table->buckets[index] = new_node;
    table->size++;

    return true;
}

void* ssn_hash_table_get(ssn_hash_table_t* table, const void* key)
{
    if (!table || !key) {
        return NULL;
    }

    uintptr_t int_key = (uintptr_t)key;
    uint32_t hash = hash_uint32((uint32_t)int_key);
    size_t index = hash % table->capacity;
    hash_node_t* node = table->buckets[index];

    while (node) {
        if (node->key == int_key) {
            return node->value;
        }
        node = node->next;
    }

    return NULL;
}

bool ssn_hash_table_remove(ssn_hash_table_t* table, const void* key)
{
    if (!table || !key) {
        return false;
    }

    uintptr_t int_key = (uintptr_t)key;
    uint32_t hash = hash_uint32((uint32_t)int_key);
    size_t index = hash % table->capacity;
    hash_node_t* node = table->buckets[index];
    hash_node_t* prev = NULL;

    while (node) {
        if (node->key == int_key) {
            break;
        }
        prev = node;
        node = node->next;
    }

    if (!node) {
        return false;
    }

    if (prev) {
        prev->next = node->next;
    } else {
        table->buckets[index] = node->next;
    }

    free(node);
    table->size--;

    return true;
}

bool ssn_hash_table_contains(ssn_hash_table_t* table, const void* key)
{
    return ssn_hash_table_get(table, key) != NULL;
}

size_t ssn_hash_table_size(const ssn_hash_table_t* table)
{
    return table ? table->size : 0;
}

size_t ssn_hash_table_capacity(const ssn_hash_table_t* table)
{
    return table ? table->capacity : 0;
}

bool ssn_hash_table_is_empty(const ssn_hash_table_t* table)
{
    return table ? table->size == 0 : true;
}

void ssn_hash_table_foreach(ssn_hash_table_t* table,
                           ssn_hash_table_foreach_cb callback,
                           void* user_data)
{
    if (!table || !callback) {
        return;
    }

    for (size_t i = 0; i < table->capacity; i++) {
        hash_node_t* node = table->buckets[i];
        while (node) {
            if (!callback((void*)node->key, node->value, user_data)) {
                return;
            }
            node = node->next;
        }
    }
}

uint32_t ssn_hash_string(const char* str)
{
    if (!str) {
        return 0;
    }

    uint32_t hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash;
}

uint32_t ssn_hash_int(const void* key)
{
    return (uint32_t)(*(const int*)key);
}

uint32_t ssn_hash_pointer(const void* ptr)
{
    uintptr_t addr = (uintptr_t)ptr;
    return (uint32_t)(addr ^ (addr >> 16));
}

