#ifndef NESTDAQ_LOGICFILTER_TIMING_H
#define NESTDAQ_LOGICFILTER_TIMING_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace LogicFilterTiming {
using Clock = std::chrono::steady_clock;
enum Stage : std::size_t {
    Receive, Input, BuildHBF, ClearMain, ClearSub, Mark, SubAnd, Lut,
    Result, Dump, Diagnostic, Output, DqmCopy, DqmSend, Send, Other,
    Total, Legacy, Count
};
inline constexpr std::array<const char*, Count> Names {{
    "recv", "input", "build_hbf", "clear_main", "clear_sub", "mark",
    "sub_and", "lut", "result", "dump", "diagnostic", "output_make",
    "dqm_copy", "dqm_send", "send", "other", "total", "legacy"
}};
inline double Microseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}
struct Frame {
    std::array<double, Count> us {};
};
// No clock access when the caller passes nullptr (profiling disabled).
class Scope {
public:
    Scope(Frame* frame, Stage stage) : fFrame(frame), fStage(stage) {
        if (fFrame) fStart = Clock::now();
    }
    ~Scope() {
        if (fFrame) fFrame->us[fStage] += Microseconds(fStart, Clock::now());
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Frame* fFrame;
    Stage fStage;
    Clock::time_point fStart {};
};
class Profiler {
    struct Stats {
        std::uint64_t tf = 0, execCalls = 0, triggers = 0;
        std::array<double, Count> sum {}, maximum {};
    };
public:
    void Configure(unsigned intervalMs) { fIntervalMs = intervalMs; Reset(); }
    bool Enabled() const { return fIntervalMs != 0; }
    void Reset() {
        fStats = {};
        fTimeouts = 0;
        fIdleReceiveUs = 0;
        fLast = Clock::now();
    }
    void Record(Frame frame, std::uint64_t triggers, std::uint64_t execCalls) {
        if (!Enabled()) return;
        // Receive precedes Total; Legacy overlaps the compute stages. Neither
        // is included in the additive stage sum used to calculate Other.
        double accounted = 0;
        for (std::size_t i = Input; i < Other; ++i) accounted += frame.us[i];
        frame.us[Other] = std::max(0.0, frame.us[Total] - accounted);
        auto& stats = fStats[triggers != 0 ? 1 : 0];
        ++stats.tf;
        stats.execCalls += execCalls;
        stats.triggers += triggers;
        for (std::size_t i = 0; i < Count; ++i) {
            stats.sum[i] += frame.us[i];
            stats.maximum[i] = std::max(stats.maximum[i], frame.us[i]);
        }
    }
    void RecordIdle(const Frame& frame) {
        if (!Enabled()) return;
        ++fTimeouts;
        fIdleReceiveUs += frame.us[Receive];
    }
    void Report(const std::string& id, bool force = false) {
        if (!Enabled()) return;
        const auto now = Clock::now();
        const double windowMs = Microseconds(fLast, now) / 1000.0;
        if (!force && windowMs < fIntervalMs) return;
        for (std::size_t kind = 0; kind < fStats.size(); ++kind) {
            const auto& stats = fStats[kind];
            if (stats.tf == 0) continue;
            std::ostringstream line;
            line << std::fixed << std::setprecision(3)
                 << "#LFProfile id=" << id
                 << " class=" << (kind ? "hits" : "no_hits")
                 << " tf=" << stats.tf << " exec_calls=" << stats.execCalls
                 << " triggers=" << stats.triggers << " window_ms=" << windowMs;
            for (std::size_t i = 0; i < Count; ++i)
                line << ' ' << Names[i] << "_mean_us=" << stats.sum[i] / stats.tf
                     << ' ' << Names[i] << "_max_us=" << stats.maximum[i];
            std::cout << line.str() << '\n';
        }
        if (fTimeouts != 0) {
            std::ostringstream line;
            line << std::fixed << std::setprecision(3)
                 << "#LFProfileIdle id=" << id << " receive_nonpositive=" << fTimeouts
                 << " recv_total_us=" << fIdleReceiveUs << " window_ms=" << windowMs;
            std::cout << line.str() << '\n';
        }
        Reset();
    }
private:
    unsigned fIntervalMs = 5000;
    std::array<Stats, 2> fStats {};
    std::uint64_t fTimeouts = 0;
    double fIdleReceiveUs = 0;
    Clock::time_point fLast = Clock::now();
};
} // namespace LogicFilterTiming
#endif
