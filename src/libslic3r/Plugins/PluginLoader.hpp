///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef plugins_pluginloader_hpp_
#define plugins_pluginloader_hpp_

namespace boost {
namespace filesystem {
class path;
}
}

namespace Slic3r {

// Register built-in plugins, load plugin libraries from the runtime plugin
// repository, activate the configured subset, then initialize active plugins.
void load_plugins();

} // namespace Slic3r

#endif // plugins_pluginloader_hpp_
