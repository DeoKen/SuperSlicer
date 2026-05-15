///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef steps_steprunner_hpp_
#define steps_steprunner_hpp_

#include <cassert>
#include <string>
#include <vector>

#include <boost/log/trivial.hpp>

#include "libslic3r/Api/host/Orchestrator.hpp"
#include "libslic3r/Api/host/Plugin.hpp"
#include "libslic3r/DataTreeFwd.hpp"
#include "libslic3r/Thread.hpp"

namespace Slic3r::Steps::Detail {

inline void validate_or_report(bool (*validator)(const Print &, std::string &),
                               const Print &print,
                               const char *validation_name)
{
    std::string error;
    if (validator(print, error))
        return;

    BOOST_LOG_TRIVIAL(error) << validation_name << " failed:\n" << error;
    assert(false && "Step data tree validation failed");
}

} // namespace Slic3r::Steps::Detail

#endif // steps_steprunner_hpp_
