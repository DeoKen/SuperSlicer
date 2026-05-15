///|/ Copyright (c) SuperSlicer 2026 Durand Rémi @supermerill
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, David Kocík @kocikdav, Tomáš Mészáros @tamasmeszaros, Vojtěch Král @vojtechkral, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) 2018 fredizzimo @fredizzimo
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/
///|/ ported from lib/Slic3r/Config.pm:
///|/ Copyright (c) Prusa Research 2016 - 2022 Vojtěch Bubník @bubnikv
///|/ Copyright (c) 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) Slic3r 2011 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Alexander Rössler @machinekoder
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/ Copyright (c) 2012 Mark Hindess
///|/ Copyright (c) 2012 Josh McCullough
///|/ Copyright (c) 2011 - 2012 Michael Moon
///|/ Copyright (c) 2012 Simon George
///|/ Copyright (c) 2012 Johannes Reinhardt
///|/ Copyright (c) 2011 Clarence Risher
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/ SuperSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ConfigDef.hpp"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/erase.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/config.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/nowide/cenv.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <LibBGCode/binarize/binarize.hpp>

#include "Flow.hpp"
#include "format.hpp"
#include "LocalesUtils.hpp"
#include "Preset.hpp"
#include "Utils.hpp"

#define L(s) (s)

namespace Slic3r {

PrinterTechnology parse_printer_technology(const std::string &technology) {
    if (technology == "FFF")
        return PrinterTechnology::ptFFF;
    else if (technology == "SLA")
        return PrinterTechnology::ptSLA;
    else if (technology == "SLS")
        return PrinterTechnology::ptSLS;
    else if (technology == "MILL")
        return PrinterTechnology::ptMill;
    else if (technology == "LASER")
        return PrinterTechnology::ptLaser;
    return PrinterTechnology::ptUnknown;
}

std::string to_string(PrinterTechnology tech) {
    if (tech == PrinterTechnology::ptFFF)
        return "FFF";
    else if (tech == PrinterTechnology::ptSLA)
        return "SLA";
    else if (tech == PrinterTechnology::ptSLS)
        return "SLS";
    else if (tech == PrinterTechnology::ptMill)
        return "MILL";
    else if (tech == PrinterTechnology::ptLaser)
        return "LASER";
    return "Unknown";
}

std::string toString(OptionCategory opt) {
    switch (opt) {
    case OptionCategory::none: return "";
    case OptionCategory::perimeter: return L("Perimeters & Shell");
    case OptionCategory::slicing: return L("Slicing");
    case OptionCategory::infill: return L("Infill");
    case OptionCategory::ironing: return L("Ironing PP");
    case OptionCategory::skirtBrim: return L("Skirt & Brim");
    case OptionCategory::support: return L("Support material");
    case OptionCategory::width: return L("Width & Flow");
    case OptionCategory::speed: return L("Speed");
    case OptionCategory::extruders: return L("Multiple extruders");
    case OptionCategory::output: return L("Output options");
    case OptionCategory::notes: return L("Notes");
    case OptionCategory::dependencies: return L("Dependencies");
    case OptionCategory::filament: return L("Filament");
    case OptionCategory::cooling: return L("Cooling");
    case OptionCategory::advanced: return L("Advanced");
    case OptionCategory::filoverride: return L("Filament overrides");
    case OptionCategory::customgcode: return L("Custom G-code");
    case OptionCategory::general: return L("General");
    case OptionCategory::limits: return L("Machine limits"); // if not used, no need ot ask for translation
    case OptionCategory::mmsetup: return L("Single Extruder MM Setup");
    case OptionCategory::firmware: return L("Firmware");
    case OptionCategory::pad: return L("Pad");
    case OptionCategory::padSupp: return L("Pad and Support");
    case OptionCategory::wipe: return L("Wipe Options");
    case OptionCategory::milling: return L("Milling");
    case OptionCategory::hollowing: return "Hollowing";
    case OptionCategory::milling_extruders: return L("Milling tools");
    case OptionCategory::fuzzy_skin : return L("Fuzzy skin");
    }
    return "error";
}


// Escape \n, \r and backslash
std::string escape_string_cstyle(const std::string &str)
{
    // Allocate a buffer twice the input string length,
    // so the output will fit even if all input characters get escaped.
    std::vector<char> out(str.size() * 2, 0);
    char *outptr = out.data();
    for (size_t i = 0; i < str.size(); ++ i) {
        char c = str[i];
        if (c == '\r') {
            (*outptr ++) = '\\';
            (*outptr ++) = 'r';
        } else if (c == '\n') {
            (*outptr ++) = '\\';
            (*outptr ++) = 'n';
        } else if (c == '\\') {
            (*outptr ++) = '\\';
            (*outptr ++) = '\\';
        } else
            (*outptr ++) = c;
    }
    return std::string(out.data(), outptr - out.data());
}

std::string escape_strings_cstyle(const std::vector<std::string> &strs)
{
    return escape_strings_cstyle(strs, {});
}

std::string escape_strings_cstyle(const std::vector<std::string> &strs, const std::vector<bool> &enables)
{
    assert(strs.size() == enables.size() || enables.empty());
    // 1) Estimate the output buffer size to avoid buffer reallocation.
    size_t outbuflen = 0;
    for (size_t i = 0; i < strs.size(); ++ i)
        // Reserve space for every character escaped + quotes + semicolon + enable.
        outbuflen += strs[i].size() * 2 + ((enables.empty() || enables[i]) ? 3 : 4);
    // 2) Fill in the buffer.
    std::vector<char> out(outbuflen, 0);
    char *outptr = out.data();
    for (size_t j = 0; j < strs.size(); ++ j) {
        if (j > 0)
            // Separate the strings.
            (*outptr ++) = ';';
        if (!(enables.empty() || enables[j])) {
            (*outptr++) = '!';
            (*outptr++) = ':';
        }
        const std::string &str = strs[j];
        // Is the string simple or complex? Complex string contains spaces, tabs, new lines and other
        // escapable characters. Empty string shall be quoted as well, if it is the only string in strs.
        bool should_quote = strs.size() == 1 && str.empty();
        for (size_t i = 0; i < str.size(); ++ i) {
            char c = str[i];
            if (c == ' ' || c == ';' || c == ',' || c == '\t' || c == '\\' || c == '"' || c == '\r' || c == '\n') {
                should_quote = true;
                break;
            }
        }
        if (should_quote) {
            (*outptr ++) = '"';
            for (size_t i = 0; i < str.size(); ++ i) {
                char c = str[i];
                if (c == '\\' || c == '"') {
                    (*outptr ++) = '\\';
                    (*outptr ++) = c;
                } else if (c == '\r') {
                    (*outptr ++) = '\\';
                    (*outptr ++) = 'r';
                } else if (c == '\n') {
                    (*outptr ++) = '\\';
                    (*outptr ++) = 'n';
                } else
                    (*outptr ++) = c;
            }
            (*outptr ++) = '"';
        } else {
            memcpy(outptr, str.data(), str.size());
            outptr += str.size();
        }
    }
    return std::string(out.data(), outptr - out.data());
}

// Unescape \n, \r and backslash
bool unescape_string_cstyle(const std::string &str, std::string &str_out)
{
    std::vector<char> out(str.size(), 0);
    char *outptr = out.data();
    for (size_t i = 0; i < str.size(); ++ i) {
        char c = str[i];
        if (c == '\\') {
            if (++ i == str.size())
                return false;
            c = str[i];
            if (c == 'r')
                (*outptr ++) = '\r';
            else if (c == 'n')
                (*outptr ++) = '\n';
            else
                (*outptr ++) = c;
        } else
            (*outptr ++) = c;
    }
    str_out.assign(out.data(), outptr - out.data());
    return true;
}

bool unescape_strings_cstyle(const std::string &str, std::vector<std::string> &out_values)
{
    std::vector<bool> useless;
    return unescape_strings_cstyle(str, out_values, useless);
}
bool unescape_strings_cstyle(const std::string &str, std::vector<std::string> &out_values, std::vector<bool> &out_enables)
{
    if (str.empty())
        return true;

    size_t i = 0;
    for (;;) {
        // Skip white spaces.
        char c = str[i];
        while (c == ' ' || c == '\t') {
            if (++ i == str.size())
                return true;
            c = str[i];
        }
        bool enable = true;
        if (c == '!' && str.size() > i + 1 && str[i + 1] == ':') {
            enable = false;
            ++i;
            c = str[++i];
        }
        // Start of a word.
        std::vector<char> buf;
        buf.reserve(16);
        // Is it enclosed in quotes?
        c = str[i];
        if (c == '"') {
            // Complex case, string is enclosed in quotes.
            for (++ i; i < str.size(); ++ i) {
                c = str[i];
                if (c == '"') {
                    // End of string.
                    break;
                }
                if (c == '\\') {
                    if (++ i == str.size())
                        return false;
                    c = str[i];
                    if (c == 'r')
                        c = '\r';
                    else if (c == 'n')
                        c = '\n';
                }
                buf.push_back(c);
            }
            if (i == str.size())
                return false;
            ++ i;
        } else {
            for (; i < str.size(); ++ i) {
                c = str[i];
                if (c == ';' || c == ',')
                    break;
                buf.push_back(c);
            }
        }
        // Store the string into the output vector.
        out_values.push_back(std::string(buf.data(), buf.size()));
        out_enables.push_back(enable);
        if (i == str.size())
            return true;
        // Skip white spaces.
        c = str[i];
        while (c == ' ' || c == '\t') {
            if (++ i == str.size())
                // End of string. This is correct.
                return true;
            c = str[i];
        }
        if (c != ';' && c != ',')
            return false;
        if (++ i == str.size()) {
            // Emit one additional empty string.
            out_values.push_back(std::string());
            out_enables.push_back(true);
            return true;
        }
    }
}

std::string escape_ampersand(const std::string& str)
{
    // Allocate a buffer 2 times the input string length,
    // so the output will fit even if all input characters get escaped.
    std::vector<char> out(str.size() * 6, 0);
    char* outptr = out.data();
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c == '&') {
            (*outptr++) = '&';
            (*outptr++) = '&';
        } else
            (*outptr++) = c;
    }
    return std::string(out.data(), outptr - out.data());
}

bool GraphData::operator<(const GraphData &rhs) const
{
    if (this->data_size() == rhs.data_size()) {
        const Pointfs my_data = this->data();
        const Pointfs other_data = rhs.data();
        assert(my_data.size() == other_data.size());
        auto it_this = my_data.begin();
        auto it_other = other_data.begin();
        while (it_this != my_data.end()) {
            if(it_this->x() != it_other->x())
                return it_this->x() < it_other->x();
            if(it_this->y() != it_other->y())
                return it_this->y() < it_other->y();
            ++it_this;
            ++it_other;
        }
        return this->type < rhs.type;
    }
    return this->data_size() < rhs.data_size();
}

bool GraphData::operator>(const GraphData &rhs) const
{
    if (this->data_size() == rhs.data_size()) {
        const Pointfs my_data = this->data();
        const Pointfs other_data = rhs.data();
        assert(my_data.size() == other_data.size());
        auto it_this = my_data.begin();
        auto it_other = other_data.begin();
        while (it_this != my_data.end()) {
            if(it_this->x() != it_other->x())
                return it_this->x() > it_other->x();
            if(it_this->y() != it_other->y())
                return it_this->y() > it_other->y();
            ++it_this;
            ++it_other;
        }
        return this->type > rhs.type;
    }
    return this->data_size() > rhs.data_size();
}

Pointfs GraphData::data() const
{
    assert(validate());
    return Pointfs(this->graph_points.begin() + this->begin_idx, this->graph_points.begin() + this->end_idx);
}

size_t GraphData::data_size() const
{
    assert(validate());
    return this->end_idx - this->begin_idx;
}

double GraphData::interpolate(double x_value) const{
    double y_value = 1.0f;
    if (this->data_size() < 1) {
        // nothing
    } else if (this->graph_points.size() == 1 || this->graph_points[begin_idx].x() >= x_value) {
        y_value = this->graph_points.front().y();
    } else if (this->graph_points[end_idx - 1].x() <= x_value) {
        y_value = this->graph_points[end_idx - 1].y();
    } else {
        // find first and second datapoint
        for (size_t idx = this->begin_idx; idx < this->end_idx; ++idx) {
            const auto &data_point = this->graph_points[idx];
            if (is_approx(data_point.x(), x_value)) {
                // lucky point
                return data_point.y();
            } else if (data_point.x() < x_value) {
                // not yet, iterate
            } else if (idx == 0) {
                return data_point.y();
            } else {
                // interpolate
                const auto &data_point_before = this->graph_points[idx - 1];
                assert(data_point.x() > data_point_before.x());
                assert(data_point_before.x() < x_value);
                assert(data_point.x() > x_value);
                if (this->type == GraphData::GraphType::SQUARE) {
                    y_value = data_point_before.y();
                } else if (this->type == GraphData::GraphType::LINEAR) {
                    const double interval     = data_point.x() - data_point_before.x();
                    const double ratio_before = (x_value - data_point_before.x()) / interval;
                    double mult = data_point_before.y() * (1 - ratio_before) + data_point.y() * ratio_before;
                    y_value = mult;
                } else if (this->type == GraphData::GraphType::SPLINE) {
                    // Cubic spline interpolation: see https://en.wikiversity.org/wiki/Cubic_Spline_Interpolation#Methods
                    const bool boundary_first_derivative = true; // true - first derivative is 0 at the leftmost and
                                                                 // rightmost point false - second ---- || -------
                    // TODO: cache (if the caller use my cache).
                    const int N = end_idx - begin_idx - 1; // last point can be accessed as N, we have N+1 total points
                    std::vector<float> diag(N + 1);
                    std::vector<float> mu(N + 1);
                    std::vector<float> lambda(N + 1);
                    std::vector<float> h(N + 1);
                    std::vector<float> rhs(N + 1);

                    // let's fill in inner equations
                    for (int i = 1 + begin_idx; i <= N + begin_idx; ++i) h[i] = this->graph_points[i].x() - this->graph_points[i - 1].x();
                    std::fill(diag.begin(), diag.end(), 2.f);
                    for (int i = 1 + begin_idx; i <= N + begin_idx - 1; ++i) {
                        mu[i]     = h[i] / (h[i] + h[i + 1]);
                        lambda[i] = 1.f - mu[i];
                        rhs[i]    = 6 * (float(this->graph_points[i + 1].y() - this->graph_points[i].y()) /
                                          (h[i + 1] * (this->graph_points[i + 1].x() - this->graph_points[i - 1].x())) -
                                      float(this->graph_points[i].y() - this->graph_points[i - 1].y()) /
                                          (h[i] * (this->graph_points[i + 1].x() - this->graph_points[i - 1].x())));
                    }

                    // now fill in the first and last equations, according to boundary conditions:
                    if (boundary_first_derivative) {
                        const float endpoints_derivative = 0;
                        lambda[0]                        = 1;
                        mu[N]                            = 1;
                        rhs[0] = (6.f / h[1]) * (float(this->graph_points[begin_idx].y() - this->graph_points[1 + begin_idx].y()) /
                                                     (this->graph_points[begin_idx].x() - this->graph_points[1 + begin_idx].x()) - endpoints_derivative);
                        rhs[N] = (6.f / h[N]) * (endpoints_derivative - float(this->graph_points[N + begin_idx - 1].y() - this->graph_points[N + begin_idx].y()) /
                                                                            (this->graph_points[N + begin_idx - 1].x() - this->graph_points[N + begin_idx].x()));
                    } else {
                        lambda[0] = 0;
                        mu[N]     = 0;
                        rhs[0]    = 0;
                        rhs[N]    = 0;
                    }

                    // the trilinear system is ready to be solved:
                    for (int i = 1; i <= N; ++i) {
                        float multiple = mu[i] / diag[i - 1]; // let's subtract proper multiple of above equation
                        diag[i] -= multiple * lambda[i - 1];
                        rhs[i] -= multiple * rhs[i - 1];
                    }
                    // now the back substitution (vector mu contains invalid values from now on):
                    rhs[N] = rhs[N] / diag[N];
                    for (int i = N - 1; i >= 0; --i) rhs[i] = (rhs[i] - lambda[i] * rhs[i + 1]) / diag[i];

                    //now interpolate at our point
                    size_t curr_idx = idx - begin_idx;
                    y_value = (rhs[curr_idx - 1] * pow(this->graph_points[idx].x() - x_value, 3) +
                            rhs[curr_idx] * pow(x_value - this->graph_points[idx - 1].x(), 3)) /
                            (6 * h[curr_idx]) +
                        (this->graph_points[idx - 1].y() - rhs[curr_idx - 1] * h[curr_idx] * h[curr_idx] / 6.f) *
                            (this->graph_points[idx].x() - x_value) / h[curr_idx] +
                        (this->graph_points[idx].y() - rhs[curr_idx] * h[curr_idx] * h[curr_idx] / 6.f) *
                            (x_value - this->graph_points[idx - 1].x()) / h[curr_idx];
                } else {
                    assert(false);
                }
                return y_value;
            }
        }
    }
    return y_value;
}

double GraphData::inverse_interpolate(double y_value) const {
    GraphData inverse = *this;
    // inverse x & y
    for (Vec2d &data_point : inverse.graph_points) {
        std::swap(data_point.x(), data_point.y());
    }
    return inverse.interpolate(y_value);
}

bool GraphData::validate() const
{
    if (this->begin_idx < 0 || this->end_idx < 0 || this->end_idx < this->begin_idx)
        return false;
    if (this->end_idx > this->graph_points.size() && !this->graph_points.empty())
        return false;
    if(this->graph_points.empty())
        return this->end_idx == 0 && this->begin_idx == 0;
    for (size_t i = 1; i < this->graph_points.size(); ++i)
        if (this->graph_points[i - 1].x() > this->graph_points[i].x())
            return false;
    return true;
}

std::string GraphData::serialize() const
{
    std::ostringstream ss;
    ss << this->begin_idx;
    ss << ":";
    ss << this->end_idx;
    ss << ":";
    ss << uint16_t(this->type);
    for (const Vec2d &graph_point : this->graph_points) {
        ss << ":";
        ss << graph_point.x();
        ss << "x";
        ss << graph_point.y();
    }
    return ss.str();
}
    
bool GraphData::deserialize(const std::string &str)
{
    if (size_t pos = str.find('|'); pos != std::string::npos) {
        // old format
        assert(str.size() > pos + 2);
        assert(str[pos+1] == ' ');
        assert(str[pos+2] != ' ');
        if (str.size() > pos + 1) {
            std::string buttons = str.substr(pos + 2);
            size_t start = 0;
            size_t end_x = buttons.find(' ', start);
            size_t end_y= buttons.find(' ', end_x + 1);
            while (end_x != std::string::npos && end_y != std::string::npos) {
                this->graph_points.emplace_back();
                Vec2d &data_point = this->graph_points.back();
                data_point.x() = std::stod(buttons.substr(start, end_x));
                data_point.y() = std::stod(buttons.substr(end_x + 1, end_y));
                start = end_y + 1;
                end_x = buttons.find(' ', start);
                end_y= buttons.find(' ', end_x + 1);
            }
            if (end_x != std::string::npos && end_x + 1 < buttons.size()) {
                this->graph_points.emplace_back();
                Vec2d &data_point = this->graph_points.back();
                data_point.x() = std::stod(buttons.substr(start, end_x));
                data_point.y() = std::stod(buttons.substr(end_x + 1, buttons.size()));
            }
        }
        this->begin_idx = 0;
        this->end_idx = this->graph_points.size();
        this->type = GraphType::SPLINE;
    } else if (size_t pos = str.find(','); pos != std::string::npos) {
        //maybe a coStrings with 0,0 values inside, like a coPoints but worse (used by orca's small_area_infill_flow_compensation_model)
        std::vector<std::string> args;
        boost::split(args, str, boost::is_any_of(","));
        if (args.size() % 2 == 0) {
            for (size_t i = 0; i < args.size(); i += 2) {
                this->graph_points.emplace_back();
                Vec2d &data_point = this->graph_points.back();
                args[i].erase(std::remove(args[i].begin(), args[i].end(), '\n'), args[i].end());
                args[i].erase(std::remove(args[i].begin(), args[i].end(), '"'), args[i].end());
                data_point.x() = std::stod(args[i]);
                args[i+1].erase(std::remove(args[i+1].begin(), args[i+1].end(), '\n'), args[i+1].end());
                args[i+1].erase(std::remove(args[i+1].begin(), args[i+1].end(), '"'), args[i+1].end());
                data_point.y() = std::stod(args[i+1]);
            }
        }
        this->begin_idx = 0;
        this->end_idx = this->graph_points.size();
        this->type = GraphType::SPLINE;
    } else {
        std::istringstream iss(str);
        std::string              item;
        char                     sep_point = 'x';
        char                     sep       = ':';
        std::vector<std::string> values_str;
        // get begin_idx
        if (std::getline(iss, item, sep)) {
            std::istringstream(item) >> this->begin_idx;
        } else
            return false;
        // get end_idx
        if (std::getline(iss, item, sep)) {
            std::istringstream(item) >> this->end_idx;
        } else
            return false;
        // get type
        if (std::getline(iss, item, sep)) {
            uint16_t int_type;
            std::istringstream(item) >> int_type;
            this->type = GraphType(int_type);
        } else
            return false;
        // get points
        while (std::getline(iss, item, sep)) {
            this->graph_points.emplace_back();
            Vec2d &data_point = this->graph_points.back();
            std::string                s_point;
            std::istringstream         isspoint(item);
            if (std::getline(isspoint, s_point, sep_point)) {
                std::istringstream(s_point) >> data_point.x();
            } else
                return false;
            if (std::getline(isspoint, s_point, sep_point)) {
                std::istringstream(s_point) >> data_point.y();
            } else
                return false;
        }
    }
    //check if data is okay
    if (!this->validate()) return false;
    return true;
}

//TODO: replace ConfigOptionDef* by ConfigOptionDef&
ConfigSubstitution::ConfigSubstitution(const ConfigOptionDef *def, std::string old, ConfigOptionUniquePtr &&new_v)
    : opt_def(def), old_name(def->opt_key), old_value(old), new_value(std::move(new_v)) { assert(def); }

std::optional<ConfigSubstitution> ConfigSubstitutionContext::find(const std::string &old_name) {
    for (const ConfigSubstitution & conf: m_substitutions) {
        if(old_name == conf.old_name)
            return std::make_optional<ConfigSubstitution>(conf.old_name, conf.old_value);
    }
    return {};
}
bool ConfigSubstitutionContext::erase(std::string old_name) {
    for (size_t idx_susbst = 0; idx_susbst < m_substitutions.size(); ++idx_susbst) {
        if (old_name == m_substitutions[idx_susbst].old_name) {
            m_substitutions.erase(m_substitutions.begin() + idx_susbst);
            return true;
        }
    }
    return false;
}

void ConfigOptionDeleter::operator()(ConfigOption* p) {
    delete p;
}

}

#include <cereal/types/polymorphic.hpp>
CEREAL_REGISTER_TYPE(Slic3r::ConfigOption)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionSingle<double>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionSingle<int32_t>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionSingle<std::string>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionSingle<Slic3r::Vec2d>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionSingle<Slic3r::Vec3d>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionSingle<bool>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionVectorBase)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionVector<double>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionVector<int32_t>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionVector<std::string>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionVector<Slic3r::Vec2d>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionVector<unsigned char>)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionFloat)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionFloats)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionInt)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionInts)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionString)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionStrings)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionPercent)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionPercents)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionFloatOrPercent)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionFloatsOrPercents)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionPoint)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionPoints)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionPoint3)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionGraph)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionGraphs)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionBool)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionBools)
CEREAL_REGISTER_TYPE(Slic3r::ConfigOptionEnumGeneric)
CEREAL_REGISTER_TYPE(Slic3r::ConfigBase)
CEREAL_REGISTER_TYPE(Slic3r::DynamicConfig)

CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOption, Slic3r::ConfigOptionSingle<double>) 
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOption, Slic3r::ConfigOptionSingle<int32_t>) 
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOption, Slic3r::ConfigOptionSingle<std::string>) 
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOption, Slic3r::ConfigOptionSingle<Slic3r::Vec2d>)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOption, Slic3r::ConfigOptionSingle<Slic3r::Vec3d>)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOption, Slic3r::ConfigOptionSingle<Slic3r::GraphData>)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOption, Slic3r::ConfigOptionSingle<bool>) 
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOption, Slic3r::ConfigOptionVectorBase) 
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVectorBase, Slic3r::ConfigOptionVector<double>)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVectorBase, Slic3r::ConfigOptionVector<int32_t>)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVectorBase, Slic3r::ConfigOptionVector<std::string>)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVectorBase, Slic3r::ConfigOptionVector<Slic3r::Vec2d>)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVectorBase, Slic3r::ConfigOptionVector<Slic3r::GraphData>)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVectorBase, Slic3r::ConfigOptionVector<unsigned char>)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionSingle<double>, Slic3r::ConfigOptionFloat)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVector<double>, Slic3r::ConfigOptionFloats)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionSingle<int32_t>, Slic3r::ConfigOptionInt)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVector<int32_t>, Slic3r::ConfigOptionInts)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionSingle<std::string>, Slic3r::ConfigOptionString)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVector<std::string>, Slic3r::ConfigOptionStrings)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionFloat, Slic3r::ConfigOptionPercent)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionFloats, Slic3r::ConfigOptionPercents)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionPercent, Slic3r::ConfigOptionFloatOrPercent)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVector<Slic3r::FloatOrPercent>, Slic3r::ConfigOptionFloatsOrPercents)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionSingle<Slic3r::Vec2d>, Slic3r::ConfigOptionPoint)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVector<Slic3r::Vec2d>, Slic3r::ConfigOptionPoints)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionSingle<Slic3r::Vec3d>, Slic3r::ConfigOptionPoint3)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionSingle<Slic3r::GraphData>, Slic3r::ConfigOptionGraph)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVector<Slic3r::GraphData>, Slic3r::ConfigOptionGraphs)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionSingle<bool>, Slic3r::ConfigOptionBool)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionVector<unsigned char>, Slic3r::ConfigOptionBools)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigOptionInt, Slic3r::ConfigOptionEnumGeneric)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ConfigBase, Slic3r::DynamicConfig)
