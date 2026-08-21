/*
 * ssn_export.h - Export macros for cd-ipc-ssn library
 *
 * This file defines macros for exporting symbols from the library.
 */

#ifndef SSN_EXPORT_H
#define SSN_EXPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SSN_API - Macro for exporting/importing symbols
 *
 * 构建库时与使用时均为 default visibility；附加 used 属性防止 -O3 +
 * -fvisibility=hidden 下「可内联且无外部引用的导出函数被 GCC 局部化」
 * （缺陷背景：ssn_create_header 等被库内部高频内联调用后，readelf 显示
 * 变为 LOCAL，外部 find_package 消费者链接失败——P1-1 回归）。
 * noinline：GCC -O3 下简单函数（如 ssn_create_header/ssn_ecode_message）
 * 若被内联进调用方，IPA 路径会丢弃声明处的 default 可见性（符号残留为
 * GLOBAL HIDDEN）——noinline 强制保留独立函数体与声明可见性。
 */

#define SSN_API __attribute__((visibility("default"), used, noinline))

/* C++ 服务框架导出宏：libssn_framework 用 CXX_VISIBILITY_PRESET hidden，
 * 公开类必须显式标记导出（与 ssn_transport 的可见性策略对齐） */
#define SSN_FRAMEWORK_API __attribute__((visibility("default"), used))

#ifdef __cplusplus
}
#endif

#endif /* SSN_EXPORT_H */