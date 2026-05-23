///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_utils_h_
#define slic3r_utils_h_

#include <stdint.h>

#if defined(_WIN32) && defined(SLIC3R_HOST_EXPORTS)
#define SLIC3R_HOST_API __declspec(dllexport)
#else
#define SLIC3R_HOST_API
#endif

extern "C" {

// handle type for "temprary" storage
typedef struct storage_handle storage_handle;

/* release all objects that were created in the storage */
SLIC3R_HOST_API void storage_clear(storage_handle *me);
/* return 1 if the handle_to_check is an element of the storage, 0 otherwise */
SLIC3R_HOST_API int32_t is_local_storage(storage_handle *me, void* handle_to_check);
/* release an element with handle_to_free to check if it's correct */
SLIC3R_HOST_API int32_t storage_free(storage_handle *me, void* handle_to_free);
/* for debugging purposes, return the number of elements currently stored in the storage */
SLIC3R_HOST_API int32_t storage_size(storage_handle *me);



/* const Array of strings */
typedef struct const_strings_t {
    const char * const *items;
    uint32_t size;
} const_strings_t;


/* tbb */
SLIC3R_HOST_API void slic3r_parallel_for(uint32_t begin, uint32_t end, void *user_data, void (*fn)(uint32_t index, void *user_data));

}
#endif // slic3r_utils_h_
