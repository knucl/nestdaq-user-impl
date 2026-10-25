#ifndef NESTDAQ_SPILL_GATE_H
#define NESTDAQ_SPILL_GATE_H

#include <cstddef>
#include <cstdint>
#include <cstring>

enum class SpillGate { Unknown, Off, On };

// Unknown deliberately selects ON triggers; retain the original state for logs.
constexpr uint32_t SpillGateStateMask(SpillGate gate)
{
    return gate == SpillGate::Off ? 2u : 1u;
}

// Collect frame flags only from modern (femType 5/6) HBD1 records.
// The normal supported layouts place HBD1 at the beginning of a delimiter part
// or at the tail of an HBF part (before HBD2). Never scan the full hit payload.
class SpillGateEvidence {
public:
    void AddWord(uint64_t word, uint32_t femType, int bit, bool activeHigh)
    {
        if (bit < 0 || bit > 15 || (femType != 5 && femType != 6)
            || (word >> 58) != 0x1cu) return;
        const uint32_t frame = static_cast<uint32_t>(word & 0xffffffu);
        const bool level = ((word >> (40 + bit)) & 1u) != 0;
        const SpillGate state = level == activeHigh ? SpillGate::On : SpillGate::Off;
        if (!fSeen) {
            fSeen = true;
            fFrame = frame;
            fState = state;
        } else if (frame != fFrame || state != fState) {
            fConflict = true;
        }
    }

    void AddPart(const void* data, std::size_t bytes, uint32_t femType,
                 int bit, bool activeHigh)
    {
        if (bytes < sizeof(uint64_t) || bytes % sizeof(uint64_t) != 0) return;
        const std::size_t n = bytes / sizeof(uint64_t);
        const std::size_t positions[] = {0, n >= 2 ? n - 2 : 0, n - 1};
        for (unsigned j = 0; j < 3; ++j) {
            bool duplicate = false;
            for (unsigned k = 0; k < j; ++k) duplicate |= positions[k] == positions[j];
            if (duplicate) continue;
            uint64_t word;
            std::memcpy(&word, static_cast<const char*>(data) + positions[j] * 8, 8);
            AddWord(word, femType, bit, activeHigh);
        }
    }

    SpillGate State() const { return !fSeen || fConflict ? SpillGate::Unknown : fState; }
    bool Conflict() const { return fConflict; }

private:
    bool fSeen = false;
    bool fConflict = false;
    uint32_t fFrame = 0;
    SpillGate fState = SpillGate::Unknown;
};
#endif
