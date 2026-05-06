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
 * When building the library: SSN_API=__attribute__((visibility("default")))
 * When using the library:   SSN_API=__attribute__((visibility("default")))
 */

#ifdef SSN_BUILDING_LIBRARY
#define SSN_API __attribute__((visibility("default")))
#else
#define SSN_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
}
#endif

#endif /* SSN_EXPORT_H */