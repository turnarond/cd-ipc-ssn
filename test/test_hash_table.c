/*
 * test_hash_table.c - 哈希表单元测试（含字符串键 API 回归）
 *
 * 回归：Issue #16——原 set/get/remove 按 key 指针值比较，字符串键（RPC 方法名/
 *       主题名）用调用者指针作 key：内容相同地址不同的字符串注销静默失败；
 *       栈/临时 buffer 注册后 key 悬挂；重复注册同名项旧值泄漏。
 * 验证点：
 *   Test 1: 字符串键内容相等（地址不同）可 get/remove（内容哈希）
 *   Test 2: 重复注册同名键更新值（不产生重复节点）
 *   Test 3: 栈 buffer 注册后表内 key 独立（buffer 复用不破坏表）
 *   Test 4: 整数/指针键 API 保持原语义（向后兼容）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/ssn_hash_table.h"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { g_tests_passed++; printf("[PASS] %s\n", msg); } \
        else { g_tests_failed++; printf("[FAIL] %s\n", msg); } \
    } while (0)

/* ---- Test 1: 字符串键内容相等（地址不同）可查可删 ---- */
static void test_str_key_content_equal(void)
{
    printf("Test 1: 字符串键内容相等（地址不同）...\n");
    ssn_hash_table_t *t = ssn_hash_table_create(4);
    CHECK(t != NULL, "创建哈希表");

    char a[8];
    strcpy(a, "topic");
    char b[8];
    strcpy(b, "topic");   /* 内容相同但地址不同 */

    int v1 = 1;
    CHECK(ssn_hash_table_set_str(t, a, &v1), "用栈 buffer a 注册");
    /* 缺陷背景：原实现按指针比较，get(b) 返回 NULL（注销/查询静默失败） */
    CHECK(ssn_hash_table_get_str(t, b) == &v1,
          "用内容相同但地址不同的 b 可查到（内容哈希）");
    CHECK(ssn_hash_table_contains_str(t, "topic"), "contains 按内容命中");
    CHECK(ssn_hash_table_size(t) == 1, "size=1");

    CHECK(ssn_hash_table_remove_str(t, b), "用 b 可删除（内容哈希）");
    CHECK(ssn_hash_table_size(t) == 0, "删除后 size=0");
    CHECK(!ssn_hash_table_contains_str(t, "topic"), "删除后 contains 为 false");

    ssn_hash_table_destroy(t);
}

/* ---- Test 2: 重复注册同名键更新值 ---- */
static void test_str_key_duplicate(void)
{
    printf("Test 2: 重复注册同名键更新值...\n");
    ssn_hash_table_t *t = ssn_hash_table_create(4);

    int v1 = 1, v2 = 2;
    CHECK(ssn_hash_table_set_str(t, "dup", &v1), "首次注册");
    CHECK(ssn_hash_table_set_str(t, "dup", &v2), "重复注册同名键");
    CHECK(ssn_hash_table_size(t) == 1, "无重复节点（size 仍为 1）");
    CHECK(ssn_hash_table_get_str(t, "dup") == &v2, "值被更新为 v2");

    ssn_hash_table_destroy(t);
}

/* ---- Test 3: 栈 buffer 注册后表内 key 独立 ---- */
static void test_str_key_buffer_independent(void)
{
    printf("Test 3: 栈 buffer 注册后表内 key 独立...\n");
    ssn_hash_table_t *t = ssn_hash_table_create(4);

    char buf[16];
    strcpy(buf, "temp");
    int v = 42;
    CHECK(ssn_hash_table_set_str(t, buf, &v), "用栈 buffer 注册");

    /* 复用 buffer 写入不同内容：表内 key 应为注册时的内容副本（缺陷背景：
     * 原实现 key 悬挂指向 buf，内容变化后表数据损坏） */
    strcpy(buf, "other");
    CHECK(ssn_hash_table_contains_str(t, "temp"), "原 key 'temp' 仍可命中");
    CHECK(!ssn_hash_table_contains_str(t, "other"), "新内容 'other' 不命中");

    /* 表内 key 生命周期独立：buf 超出作用域后仍可查（此处同作用域验证内容隔离） */
    CHECK(ssn_hash_table_get_str(t, "temp") == &v, "值完整");

    ssn_hash_table_destroy(t);
}

/* ---- Test 4: 整数/指针键 API 保持原语义 ---- */
static void test_int_key_backward_compat(void)
{
    printf("Test 4: 整数/指针键 API 向后兼容...\n");
    ssn_hash_table_t *t = ssn_hash_table_create(4);

    int key1 = 100, v1 = 7;
    int key2 = 200, v2 = 8;
    CHECK(ssn_hash_table_set(t, &key1, &v1), "整数键注册 1");
    CHECK(ssn_hash_table_set(t, &key2, &v2), "整数键注册 2");
    CHECK(ssn_hash_table_get(t, &key1) == &v1, "整数键查询 1");
    CHECK(ssn_hash_table_get(t, &key2) == &v2, "整数键查询 2");
    CHECK(ssn_hash_table_remove(t, &key1), "整数键删除");
    CHECK(ssn_hash_table_size(t) == 1, "删除后 size=1");

    /* 字符串键与整数键可共存（统一节点结构） */
    char s[8];
    strcpy(s, "mix");
    int v3 = 9;
    CHECK(ssn_hash_table_set_str(t, s, &v3), "混合场景注册字符串键");
    CHECK(ssn_hash_table_size(t) == 2, "混合后 size=2");
    CHECK(ssn_hash_table_get_str(t, "mix") == &v3, "字符串键可查");
    CHECK(ssn_hash_table_get(t, &key2) == &v2, "整数键仍可查（共存）");

    ssn_hash_table_destroy(t);
}

/* ---- Test 5: 扩容后字符串键仍正确（>0.75 负载触发 rehash） ---- */
static void test_str_key_rehash(void)
{
    printf("Test 5: 扩容后字符串键仍正确...\n");
    ssn_hash_table_t *t = ssn_hash_table_create(4);  /* 初始容量 16，13 项触发扩容 */

    char keys[20][32];
    int vals[20];
    for (int i = 0; i < 20; i++) {
        snprintf(keys[i], sizeof(keys[i]), "key_%02d", i);
        vals[i] = i;
        CHECK(ssn_hash_table_set_str(t, keys[i], &vals[i]), "注册 key_%02d");
    }
    CHECK(ssn_hash_table_size(t) == 20, "20 项全部注册");

    /* 扩容后按内容查全部命中（缺陷背景：扩容按 key 指针重哈希，字符串键会错桶） */
    int ok = 1;
    for (int i = 0; i < 20; i++) {
        if (ssn_hash_table_get_str(t, keys[i]) != &vals[i]) {
            ok = 0;
            break;
        }
    }
    CHECK(ok, "扩容后 20 项全部按内容命中");

    CHECK(ssn_hash_table_remove_str(t, "key_07"), "扩容后删除命中项");
    CHECK(ssn_hash_table_size(t) == 19, "删除后 size=19");

    ssn_hash_table_destroy(t);
}

int main(void)
{
    printf("=== Hash Table Tests ===\n\n");
    test_str_key_content_equal();
    test_str_key_duplicate();
    test_str_key_buffer_independent();
    test_int_key_backward_compat();
    test_str_key_rehash();
    printf("\n=== Result: %d passed, %d failed ===\n",
           g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
