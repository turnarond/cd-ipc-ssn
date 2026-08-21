/*
 * SSN Hash Table Interface
 */

#ifndef SSN_HASH_TABLE_H
#define SSN_HASH_TABLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../ssn_export.h"

typedef struct ssn_hash_table ssn_hash_table_t;

typedef uint32_t (*ssn_hash_func_t)(const void* key);
typedef bool (*ssn_hash_key_equal_t)(const void* key1, const void* key2);

SSN_API ssn_hash_table_t* ssn_hash_table_create(size_t capacity);
SSN_API void ssn_hash_table_destroy(ssn_hash_table_t* table);

SSN_API bool ssn_hash_table_set(ssn_hash_table_t* table, void* key, void* value);
SSN_API void* ssn_hash_table_get(ssn_hash_table_t* table, const void* key);
SSN_API bool ssn_hash_table_remove(ssn_hash_table_t* table, const void* key);

/* ---- 字符串键 API（内容哈希 + key 复制持有） ----
 * 缺陷背景：原 set/get/remove 按 key 指针值比较，字符串键（RPC 方法名/主题名）
 * 用调用者指针作 key：内容相同地址不同的字符串注销静默失败；栈/临时 buffer
 * 注册后 key 悬挂；重复注册同名项旧值泄漏。
 * 修复：以下 API 按字符串内容哈希与比较，key 由表内节点复制持有（表内生命周期
 * 独立于调用方 buffer）。value 为调用方持有对象，表不负责释放。 */
SSN_API bool ssn_hash_table_set_str(ssn_hash_table_t* table, const char* key, void* value);
SSN_API void* ssn_hash_table_get_str(ssn_hash_table_t* table, const char* key);
SSN_API bool ssn_hash_table_remove_str(ssn_hash_table_t* table, const char* key);
SSN_API bool ssn_hash_table_contains_str(ssn_hash_table_t* table, const char* key);

SSN_API size_t ssn_hash_table_size(const ssn_hash_table_t* table);

SSN_API uint32_t ssn_hash_string(const char* str);

#endif

