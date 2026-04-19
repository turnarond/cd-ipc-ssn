/*
 * SSN Hash Table Interface
 */

#ifndef SSN_HASH_TABLE_H
#define SSN_HASH_TABLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct ssn_hash_table ssn_hash_table_t;
typedef void* (*ssn_hash_table_allocator_t)(size_t size);
typedef void (*ssn_hash_table_deallocator_t)(void* ptr);

typedef uint32_t (*ssn_hash_func_t)(const void* key);
typedef bool (*ssn_hash_key_equal_t)(const void* key1, const void* key2);

ssn_hash_table_t* ssn_hash_table_create(size_t capacity);
void ssn_hash_table_destroy(ssn_hash_table_t* table);

bool ssn_hash_table_set(ssn_hash_table_t* table, void* key, void* value);
void* ssn_hash_table_get(ssn_hash_table_t* table, const void* key);
bool ssn_hash_table_remove(ssn_hash_table_t* table, const void* key);
bool ssn_hash_table_contains(ssn_hash_table_t* table, const void* key);

size_t ssn_hash_table_size(const ssn_hash_table_t* table);
size_t ssn_hash_table_capacity(const ssn_hash_table_t* table);
bool ssn_hash_table_is_empty(const ssn_hash_table_t* table);

typedef bool (*ssn_hash_table_foreach_cb)(void* key, void* value, void* user_data);
void ssn_hash_table_foreach(ssn_hash_table_t* table,
                             ssn_hash_table_foreach_cb callback,
                             void* user_data);

uint32_t ssn_hash_string(const char* str);
uint32_t ssn_hash_int(const void* key);
uint32_t ssn_hash_pointer(const void* ptr);

#endif

