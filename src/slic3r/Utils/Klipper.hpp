///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_Klipper_hpp_
#define slic3r_Klipper_hpp_

#include <optional>
#include <string>

#include <boost/algorithm/string.hpp>

#include <wx/arrstr.h>
#include <wx/string.h>

#include "libslic3r/PrintConfig.hpp"

#include "OctoPrint.hpp"
namespace Slic3r {

class DynamicPrintConfig;

class Klipper : public OctoPrint
{
public:
    Klipper(DynamicPrintConfig *config);
    ~Klipper() override = default;

    const char* get_name() const;
};

}

#endif
