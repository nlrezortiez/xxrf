#include <xxrf/xxrf.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Options final {
    double duration_s = 15.0;
    double center_freq_mhz = 433.92;
    double sample_rate_msps = 5.0;
    double baseline_m = 0.15;

    int window_samples = 8192;
    int hop_samples = 2048;
    int sample_stride = 32;

    double min_coherence = 0.20;
    double cal_phase_deg = 5.514;
    int lna_gain = 16;
    int vga_gain = 16;
    bool amp_enable = false;

    bool hw_trigger = true;
    bool clkout_on_master = true;
    bool require_clkin_on_slave = true;
    int arm_delay_ms = 50;

    std::string out_serial;
    std::string in_serial;
    bool list_devices = false;
};

struct Aggregate final {
    std::uint64_t count = 0;
    double sum_coherence = 0.0;
    double min_coherence = std::numeric_limits<double>::infinity();
    double max_coherence = -std::numeric_limits<double>::infinity();

    double sum_power_dbfs = 0.0;
    double min_power_dbfs = std::numeric_limits<double>::infinity();
    double max_power_dbfs = -std::numeric_limits<double>::infinity();

    double min_theta_deg = std::numeric_limits<double>::infinity();
    double max_theta_deg = -std::numeric_limits<double>::infinity();

    double sum_sin = 0.0;
    double sum_cos = 0.0;

    std::uint64_t first_sample_index = 0;
    std::uint64_t last_sample_index = 0;
    bool have_sample_range = false;
};

void print_usage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n\n"
        << "Options:\n"
        << "  --duration-sec N       Measurement duration in seconds (default: 15)\n"
        << "  --freq-mhz F           Center frequency in MHz (default: 433.92)\n"
        << "  --sample-rate-msps R   Sample rate in Msps (default: 5.0)\n"
        << "  --baseline-m D         Antenna baseline in meters (default: 0.15)\n"
        << "  --window N             AoA window samples (default: 8192)\n"
        << "  --hop N                AoA hop samples (default: 2048)\n"
        << "  --stride N             AoA sample stride (default: 32)\n"
        << "  --min-coherence C      Minimum coherence (default: 0.20)\n"
        << "  --cal-phase-deg P      Phase correction applied to channel 1 (default: 5.514)\n"
        << "  --lna-db N             LNA gain in dB (default: 16)\n"
        << "  --vga-db N             VGA gain in dB (default: 16)\n"
        << "  --amp                  Enable RX amp\n"
        << "  --no-hw-trigger        Disable hardware trigger\n"
        << "  --no-clkout            Disable CLKOUT on trigger-out device\n"
        << "  --no-require-clkin     Do not require CLKIN on trigger-in device\n"
        << "  --arm-delay-ms N       Arm delay in milliseconds (default: 50)\n"
        << "  --out-serial SERIAL    Trigger-out device serial\n"
        << "  --in-serial SERIAL     Trigger-in device serial\n"
        << "  --list-devices         Print detected HackRF serials and exit\n"
        << "  --help                 Show this help\n";
}

bool parse_int(std::string_view s, int& out) {
    try {
        out = std::stoi(std::string(s));
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_double(std::string_view s, double& out) {
    try {
        out = std::stod(std::string(s));
        return true;
    } catch (...) {
        return false;
    }
}

xxrf::core::Result<std::vector<std::string>> enumerate_serials() {
    std::vector<std::string> out;
    auto list = xxrf::core::DeviceList::enumerate();
    if (!list) {
        return std::unexpected(list.error());
    }

    for (const auto& dev : list->devices()) {
        if (!dev.serial.empty()) {
            out.push_back(dev.serial);
        }
    }
    return out;
}

float signal_power_to_dbfs(double mean_p0, double mean_p1) noexcept {
    const double limiting_channel_power = std::max(1e-12, std::min(mean_p0, mean_p1));
    return static_cast<float>(10.0 * std::log10(limiting_channel_power));
}

xxrf::core::Result<xxrf::aoa::Processor> make_processor(const Options& opt) {
    xxrf::aoa::Config cfg;
    cfg.method = xxrf::aoa::Method::PhaseInterferometry;
    cfg.center_freq_hz = static_cast<std::uint64_t>((opt.center_freq_mhz * 1e6) + 0.5);
    cfg.sample_rate_hz = opt.sample_rate_msps * 1e6;
    cfg.geom.baseline_m = opt.baseline_m;
    cfg.geom.baseline_azimuth_rad = 0.0;
    cfg.win.window_samples = static_cast<std::size_t>(std::max(1, opt.window_samples));
    cfg.win.hop_samples = static_cast<std::size_t>(std::max(1, opt.hop_samples));
    cfg.win.sample_stride = static_cast<std::size_t>(std::max(1, opt.sample_stride));
    cfg.min_coherence = opt.min_coherence;
    cfg.clamp_sin = false;
    cfg.require_contiguous = false;
    cfg.apply_calibration = true;
    cfg.emit_below_quality = true;

    xxrf::aoa::Calibration cal{};
    const float cal_phase_rad = static_cast<float>(opt.cal_phase_deg * (std::numbers::pi / 180.0));
    cal.ch1_gain = {std::cos(cal_phase_rad), std::sin(cal_phase_rad)};
    return xxrf::aoa::Processor::create(cfg, cal);
}

std::string format_deg(double v) {
    return std::format("{:+.3f}", v);
}

} 

int main(int argc, char** argv) {
    Options opt;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        auto need_value = [&](const char* name) -> std::string_view {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << '\n';
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--list-devices") {
            opt.list_devices = true;
            continue;
        }
        if (arg == "--amp") {
            opt.amp_enable = true;
            continue;
        }
        if (arg == "--no-hw-trigger") {
            opt.hw_trigger = false;
            continue;
        }
        if (arg == "--no-clkout") {
            opt.clkout_on_master = false;
            continue;
        }
        if (arg == "--no-require-clkin") {
            opt.require_clkin_on_slave = false;
            continue;
        }
        if (arg == "--duration-sec") {
            if (!parse_double(need_value("--duration-sec"), opt.duration_s)) {
                std::cerr << "Invalid --duration-sec\n";
                return 2;
            }
            continue;
        }
        if (arg == "--freq-mhz") {
            if (!parse_double(need_value("--freq-mhz"), opt.center_freq_mhz)) {
                std::cerr << "Invalid --freq-mhz\n";
                return 2;
            }
            continue;
        }
        if (arg == "--sample-rate-msps") {
            if (!parse_double(need_value("--sample-rate-msps"), opt.sample_rate_msps)) {
                std::cerr << "Invalid --sample-rate-msps\n";
                return 2;
            }
            continue;
        }
        if (arg == "--baseline-m") {
            if (!parse_double(need_value("--baseline-m"), opt.baseline_m)) {
                std::cerr << "Invalid --baseline-m\n";
                return 2;
            }
            continue;
        }
        if (arg == "--window") {
            if (!parse_int(need_value("--window"), opt.window_samples)) {
                std::cerr << "Invalid --window\n";
                return 2;
            }
            continue;
        }
        if (arg == "--hop") {
            if (!parse_int(need_value("--hop"), opt.hop_samples)) {
                std::cerr << "Invalid --hop\n";
                return 2;
            }
            continue;
        }
        if (arg == "--stride") {
            if (!parse_int(need_value("--stride"), opt.sample_stride)) {
                std::cerr << "Invalid --stride\n";
                return 2;
            }
            continue;
        }
        if (arg == "--min-coherence") {
            if (!parse_double(need_value("--min-coherence"), opt.min_coherence)) {
                std::cerr << "Invalid --min-coherence\n";
                return 2;
            }
            continue;
        }
        if (arg == "--cal-phase-deg") {
            if (!parse_double(need_value("--cal-phase-deg"), opt.cal_phase_deg)) {
                std::cerr << "Invalid --cal-phase-deg\n";
                return 2;
            }
            continue;
        }
        if (arg == "--lna-db") {
            if (!parse_int(need_value("--lna-db"), opt.lna_gain)) {
                std::cerr << "Invalid --lna-db\n";
                return 2;
            }
            continue;
        }
        if (arg == "--vga-db") {
            if (!parse_int(need_value("--vga-db"), opt.vga_gain)) {
                std::cerr << "Invalid --vga-db\n";
                return 2;
            }
            continue;
        }
        if (arg == "--arm-delay-ms") {
            if (!parse_int(need_value("--arm-delay-ms"), opt.arm_delay_ms)) {
                std::cerr << "Invalid --arm-delay-ms\n";
                return 2;
            }
            continue;
        }
        if (arg == "--out-serial") {
            opt.out_serial = std::string(need_value("--out-serial"));
            continue;
        }
        if (arg == "--in-serial") {
            opt.in_serial = std::string(need_value("--in-serial"));
            continue;
        }

        std::cerr << "Unknown argument: " << arg << '\n';
        print_usage(argv[0]);
        return 2;
    }

    auto serialsr = enumerate_serials();
    if (!serialsr) {
        std::cerr << "Failed to enumerate HackRF devices: " << serialsr.error().message << '\n';
        return 1;
    }
    auto serials = std::move(*serialsr);

    if (opt.list_devices) {
        for (const auto& serial : serials) {
            std::cout << serial << '\n';
        }
        return 0;
    }

    if (serials.size() < 2) {
        std::cerr << "Need at least 2 HackRF devices\n";
        return 1;
    }

    if (opt.out_serial.empty()) {
        opt.out_serial = serials[0];
    }
    if (opt.in_serial.empty()) {
        opt.in_serial = (serials.size() >= 2) ? serials[1] : std::string{};
    }
    if (opt.out_serial == opt.in_serial) {
        std::cerr << "Trigger-out and trigger-in devices must be different\n";
        return 1;
    }

    auto procr = make_processor(opt);
    if (!procr) {
        std::cerr << "Failed to create AoA processor: " << procr.error().message << '\n';
        return 1;
    }
    auto proc = std::move(*procr);

    Aggregate agg;
    std::mutex agg_mu;

    xxrf::sync::DualRxDeviceId did_out{.serial = opt.out_serial, .role = xxrf::sync::TriggerRole::TriggerOut};
    xxrf::sync::DualRxDeviceId did_in{.serial = opt.in_serial, .role = xxrf::sync::TriggerRole::TriggerInWait};

    xxrf::aoa::rt::StreamOptions stream_opt{};
    stream_opt.require_zero_skew = false;
    stream_opt.dual.sync.enable_hardware_trigger = opt.hw_trigger;
    stream_opt.dual.sync.enable_clkout_on_trigger_out = opt.clkout_on_master;
    stream_opt.dual.sync.require_clkin_on_trigger_in = opt.require_clkin_on_slave;
    stream_opt.dual.sync.arm_delay = std::chrono::milliseconds(std::max(0, opt.arm_delay_ms));
    stream_opt.dual.pairing = xxrf::sync::PairingMode::BySampleIndex;
    stream_opt.dual.max_skew_samples = 4096;
    stream_opt.dual.staging_queue_blocks = 32;
    stream_opt.dual.stream.ring_blocks = 64;
    stream_opt.dual.stream.block_bytes = 262144;
    stream_opt.dual.settings.apply_common_settings = true;
    stream_opt.dual.settings.sample_rate_hz = proc.config().sample_rate_hz;
    stream_opt.dual.settings.center_freq_hz = proc.config().center_freq_hz;
    stream_opt.dual.settings.lna_gain_db = static_cast<std::uint32_t>(std::max(0, opt.lna_gain));
    stream_opt.dual.settings.vga_gain_db = static_cast<std::uint32_t>(std::max(0, opt.vga_gain));
    stream_opt.dual.settings.amp_enable = opt.amp_enable;

    auto streamr = xxrf::aoa::rt::Stream::start(
        did_out, did_in, std::move(proc),
        [&](const xxrf::aoa::Estimate& est) {
            const double theta_deg = est.theta_rad * (180.0 / std::numbers::pi);
            const double coh = est.quality.coherence;
            const double p_dbfs = signal_power_to_dbfs(est.quality.mean_p0, est.quality.mean_p1);

            std::lock_guard<std::mutex> lk(agg_mu);
            if (agg.count == 0) {
                agg.first_sample_index = est.sample_index;
                agg.have_sample_range = true;
            }
            agg.last_sample_index = est.sample_index;
            agg.count += 1;

            agg.min_theta_deg = std::min(agg.min_theta_deg, theta_deg);
            agg.max_theta_deg = std::max(agg.max_theta_deg, theta_deg);
            agg.sum_sin += std::sin(est.theta_rad);
            agg.sum_cos += std::cos(est.theta_rad);

            agg.sum_coherence += coh;
            agg.min_coherence = std::min(agg.min_coherence, coh);
            agg.max_coherence = std::max(agg.max_coherence, coh);

            agg.sum_power_dbfs += p_dbfs;
            agg.min_power_dbfs = std::min(agg.min_power_dbfs, p_dbfs);
            agg.max_power_dbfs = std::max(agg.max_power_dbfs, p_dbfs);
        },
        stream_opt);

    if (!streamr) {
        std::cerr << "Failed to start stream: " << streamr.error().message << '\n';
        return 1;
    }

    auto stream = std::move(*streamr);
    const auto duration = std::chrono::duration<double>(opt.duration_s);
    std::this_thread::sleep_for(duration);

    const auto stop_status = stream.stop();
    if (!stop_status) {
        std::cerr << "Stop failed: " << stop_status.error().message << '\n';
        return 1;
    }

    const auto stats = stream.stats();

    Aggregate final_agg;
    {
        std::lock_guard<std::mutex> lk(agg_mu);
        final_agg = agg;
    }

    std::cout << "# xxrf_aoa_probe report\n\n";
    std::cout << "- duration_sec: " << opt.duration_s << '\n';
    std::cout << "- out_serial: " << opt.out_serial << '\n';
    std::cout << "- in_serial: " << opt.in_serial << '\n';
    std::cout << "- center_freq_mhz: " << opt.center_freq_mhz << '\n';
    std::cout << "- sample_rate_msps: " << opt.sample_rate_msps << '\n';
    std::cout << "- baseline_m: " << opt.baseline_m << '\n';
    std::cout << "- cal_phase_deg: " << opt.cal_phase_deg << '\n';
    std::cout << "- lna_db: " << opt.lna_gain << '\n';
    std::cout << "- vga_db: " << opt.vga_gain << '\n';
    std::cout << "- amp: " << (opt.amp_enable ? "on" : "off") << '\n';
    std::cout << "- window/hop/stride: " << opt.window_samples << "/" << opt.hop_samples << "/" << opt.sample_stride
              << "\n\n";

    if (final_agg.count == 0) {
        std::cout << "No estimates collected.\n";
        return 0;
    }

    const double circ_theta_deg = std::atan2(final_agg.sum_sin, final_agg.sum_cos) * (180.0 / std::numbers::pi);
    const double avg_coherence = final_agg.sum_coherence / static_cast<double>(final_agg.count);
    const double avg_power_dbfs = final_agg.sum_power_dbfs / static_cast<double>(final_agg.count);

    std::cout << "## AoA aggregate\n\n";
    std::cout << "- estimates: " << final_agg.count << '\n';
    std::cout << "- theta_avg_deg: " << format_deg(circ_theta_deg) << '\n';
    std::cout << "- theta_circular_avg_deg: " << format_deg(circ_theta_deg) << '\n';
    std::cout << "- theta_min_deg: " << format_deg(final_agg.min_theta_deg) << '\n';
    std::cout << "- theta_max_deg: " << format_deg(final_agg.max_theta_deg) << '\n';
    std::cout << "- coherence_avg: " << std::format("{:.4f}", avg_coherence) << '\n';
    std::cout << "- coherence_min: " << std::format("{:.4f}", final_agg.min_coherence) << '\n';
    std::cout << "- coherence_max: " << std::format("{:.4f}", final_agg.max_coherence) << '\n';
    std::cout << "- power_avg_dbfs: " << std::format("{:.3f}", avg_power_dbfs) << '\n';
    std::cout << "- power_min_dbfs: " << std::format("{:.3f}", final_agg.min_power_dbfs) << '\n';
    std::cout << "- power_max_dbfs: " << std::format("{:.3f}", final_agg.max_power_dbfs) << '\n';
    std::cout << "- sample_index_first: " << final_agg.first_sample_index << '\n';
    std::cout << "- sample_index_last: " << final_agg.last_sample_index << '\n';
    std::cout << "- sample_span: " << (final_agg.last_sample_index - final_agg.first_sample_index) << "\n\n";

    std::cout << "## Stream stats\n\n";
    std::cout << "- pairs_emitted: " << stats.dual.pairs_emitted << '\n';
    std::cout << "- max_abs_skew_samples: " << stats.dual.max_abs_skew_samples << '\n';
    std::cout << "- drops_pairing_trigger_out: " << stats.dual.drops_pairing_trigger_out << '\n';
    std::cout << "- drops_pairing_trigger_in: " << stats.dual.drops_pairing_trigger_in << '\n';
    std::cout << "- rx_blocks_dropped_out: " << stats.dual.trigger_out.blocks_dropped << '\n';
    std::cout << "- rx_blocks_dropped_in: " << stats.dual.trigger_in.blocks_dropped << '\n';
    std::cout << "- aoa_frames_in: " << stats.aoa.frames_in << '\n';
    std::cout << "- aoa_samples_used: " << stats.aoa.samples_used << '\n';
    std::cout << "- aoa_estimates_emitted: " << stats.aoa.estimates_emitted << '\n';
    std::cout << "- aoa_discontinuities: " << stats.aoa.discontinuities << '\n';
    std::cout << "- aoa_invalid_geometry: " << stats.aoa.invalid_geometry << '\n';
    std::cout << "- aoa_below_quality: " << stats.aoa.below_quality << '\n';

    return 0;
}
