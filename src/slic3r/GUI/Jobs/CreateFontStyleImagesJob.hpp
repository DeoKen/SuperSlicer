///|/ Copyright (c) SuperSlicer 2026 Durand R?mi @supermerill
///|/ Copyright (c) Prusa Research 2022 Filip Sykala @Jony01
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_CreateFontStyleImagesJob_hpp_
#define slic3r_CreateFontStyleImagesJob_hpp_

#include <string>
#include <vector>

#include "libslic3r/Emboss.hpp"

#include "Job.hpp"
#include "slic3r/Utils/EmbossStyleManager.hpp"
namespace Slic3r::GUI::Emboss {

/// <summary>
/// Create texture with name of styles written by its style
/// NOTE: Access to glyph cache is possible only from job
/// </summary>
class CreateFontStyleImagesJob : public Job
{
    StyleManager::StyleImagesData m_input;

    // Output data
    // texture size
    int m_width, m_height;
    // texture data
    std::vector<unsigned char> m_pixels; 
    // descriptors of sub textures
    std::vector<StyleManager::StyleImage> m_images;

public:
    CreateFontStyleImagesJob(StyleManager::StyleImagesData &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &) override;
};

} // namespace Slic3r::GUI

#endif // slic3r_CreateFontStyleImagesJob_hpp_
