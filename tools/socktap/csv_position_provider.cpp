#include "csv_position_provider.hpp"
#include <vanetza/common/confident_quantity.hpp>
#include <vanetza/units/angle.hpp>
#include <vanetza/units/length.hpp>
#include <vanetza/units/velocity.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace vanetza;

namespace {

const units::TrueNorth north = units::TrueNorth::from_value(0.0);

/** Circular interpolation so 359 deg -> 1 deg does not sweep backwards. */
double lerp_heading(double a_deg, double b_deg, double u)
{
    const double a = a_deg * M_PI / 180.0;
    const double b = b_deg * M_PI / 180.0;
    const double s = (1.0 - u) * std::sin(a) + u * std::sin(b);
    const double c = (1.0 - u) * std::cos(a) + u * std::cos(b);
    double d = std::atan2(s, c) * 180.0 / M_PI;
    return d < 0.0 ? d + 360.0 : d;
}

} // namespace

CsvPositionProvider::CsvPositionProvider(const std::string& path,
                                         const Runtime& runtime,
                                         bool loop,
                                         double pos_confidence_m)
    : m_runtime(runtime), m_start(runtime.now()),
      m_loop(loop), m_conf(pos_confidence_m)
{
    load(path);
    if (m_samples.empty()) {
        throw std::runtime_error("CsvPositionProvider: no samples parsed from " + path);
    }
}

void CsvPositionProvider::load(const std::string& path)
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("CsvPositionProvider: cannot open " + path);
    }

    std::string line;
    std::getline(in, line); // discard header

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); // tolerate CRLF
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string field;
        Sample s{};
        auto next = [&](double& out) {
            if (!std::getline(ss, field, ',')) return false;
            out = std::stod(field);
            return true;
        };
        if (next(s.t) && next(s.lat) && next(s.lon) &&
            next(s.alt) && next(s.speed) && next(s.heading)) {
            m_samples.push_back(s);
        }
    }
    std::sort(m_samples.begin(), m_samples.end(),
              [](const Sample& a, const Sample& b) { return a.t < b.t; });
}

CsvPositionProvider::Sample CsvPositionProvider::interpolate(double t) const
{
    const double dur = m_samples.back().t;
    if (m_loop && dur > 0.0) t = std::fmod(t, dur);
    if (t <= m_samples.front().t) return m_samples.front();
    if (t >= dur) return m_samples.back();

    auto it = std::lower_bound(m_samples.begin(), m_samples.end(), t,
        [](const Sample& s, double v) { return s.t < v; });
    const Sample& b = *it;
    const Sample& a = *(it - 1);

    const double span = b.t - a.t;
    const double u = (span <= 0.0) ? 0.0 : (t - a.t) / span;

    Sample s;
    s.t       = t;
    s.lat     = a.lat   + u * (b.lat   - a.lat);
    s.lon     = a.lon   + u * (b.lon   - a.lon);
    s.alt     = a.alt   + u * (b.alt   - a.alt);
    s.speed   = a.speed + u * (b.speed - a.speed);
    s.heading = lerp_heading(a.heading, b.heading, u);
    return s;
}

const PositionFix& CsvPositionProvider::position_fix()
{
    using namespace vanetza::units;

    const double elapsed =
        std::chrono::duration_cast<std::chrono::duration<double>>(
            m_runtime.now() - m_start).count();
    const Sample s = interpolate(elapsed);

    PositionFix fix;
    fix.timestamp = m_runtime.now();
    fix.latitude  = s.lat * degree;
    fix.longitude = s.lon * degree;

    fix.confidence.semi_major  = m_conf * si::meter;
    fix.confidence.semi_minor  = m_conf * si::meter;
    fix.confidence.orientation = north;

    fix.speed.assign(s.speed * si::meter_per_second,
                     0.1     * si::meter_per_second);
    fix.course.assign(north + s.heading * degree,
                      north + 1.0       * degree);
    fix.altitude = ConfidentQuantity<Length>(s.alt * si::meter,
                                             0.2   * si::meter);

    StoredPositionProvider::position_fix(fix);
    return StoredPositionProvider::position_fix();
}
