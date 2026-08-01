#ifndef CSV_POSITION_PROVIDER_HPP_V2X
#define CSV_POSITION_PROVIDER_HPP_V2X

#include <vanetza/common/runtime.hpp>
#include <vanetza/common/stored_position_provider.hpp>
#include <string>
#include <vector>

/**
 * Replays a normalized dGPS trace as a Vanetza position source.
 *
 * Expected CSV header (produced by tools/prepare_trace.py):
 *     t_s,lat_deg,lon_deg,alt_m,speed_mps,heading_deg
 *
 * heading_deg is the azimuth, clockwise from North (ETSI/CAM convention).
 */
class CsvPositionProvider : public vanetza::StoredPositionProvider
{
public:
    struct Sample { double t, lat, lon, alt, speed, heading; };

    CsvPositionProvider(const std::string& path,
                        const vanetza::Runtime& runtime,
                        bool loop = true,
                        double pos_confidence_m = 0.1);

    /** Recomputes the fix from elapsed runtime, then returns it. */
    const vanetza::PositionFix& position_fix() override;

    std::size_t size() const { return m_samples.size(); }
    double duration() const { return m_samples.empty() ? 0.0 : m_samples.back().t; }

private:
    void load(const std::string& path);
    Sample interpolate(double t) const;

    const vanetza::Runtime& m_runtime;
    std::vector<Sample> m_samples;
    vanetza::Clock::time_point m_start;
    bool m_loop;
    double m_conf;
};

#endif /* CSV_POSITION_PROVIDER_HPP_V2X */
