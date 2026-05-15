///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/Api/plugin/c/slic3r_utils.h"
#include "libslic3r/Thread.hpp"

#include "Orchestrator.hpp"

/* tbb */
void slic3r_parallel_for(uint32_t begin, uint32_t end, void *user_data, void (*fn)(uint32_t index, void *user_data)) {
    Slic3r::parallel_for(begin, end, [&](size_t i) { fn(uint32_t(i), user_data); });
}


/* release all objects that were created in the storage */
void storage_clear(storage_handle *me) {
    Slic3r::PluginStorage *storage = reinterpret_cast<Slic3r::PluginStorage *>(me);
    storage->clear();
}
/* return 1 if the handle_to_check is an element of the storage, 0 otherwise */
int32_t is_local_storage(storage_handle *me, void *handle_to_check) {
    Slic3r::PluginStorage *storage = reinterpret_cast<Slic3r::PluginStorage *>(me);
    return storage->contains(handle_to_check);
}
/* release an element with handle_to_free to check if it's correct */
int32_t storage_free(storage_handle *me, void *handle_to_free) {
    Slic3r::PluginStorage *storage = reinterpret_cast<Slic3r::PluginStorage *>(me);
    return storage->free(handle_to_free);
}

int32_t storage_size(storage_handle *me) {
    Slic3r::PluginStorage *storage = reinterpret_cast<Slic3r::PluginStorage *>(me);
    return uint32_t(storage->size());
}
