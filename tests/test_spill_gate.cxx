// g++ -std=c++17 -O2 -I. tests/test_spill_gate.cxx -o /tmp/test_spill_gate
#include "Trigger.cxx"
#include "HeartbeatFrameHeader.h"
#include <cassert>
#include <iostream>

static uint64_t hbd(uint32_t frame, uint16_t flags)
{
    return (0x1cULL << 58) | (uint64_t{flags} << 40) | frame;
}

int main()
{
    TriggerMap map;
    map.MakeTable("0-2-1:0; 1-3-2:0; 2-1-3:0; 3-1-0:0");
    assert(map.LookUpTriggerFlags(1) == 15);
    assert(map.GetEnabledTriggerMask(SpillGate::On) == 5);
    assert(map.GetEnabledTriggerMask(SpillGate::Off) == 6);
    assert(map.GetEnabledTriggerMask(SpillGate::Unknown) == 5);
    for (auto bad : {"0-1-4:0", "0-1--1:0", "0-1-:0", "0--1:0", "0-1-2-3:0"}) {
        bool rejected = false;
        try { map.MakeTable(bad); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
        assert(map.GetEnabledTriggerMask(SpillGate::On) == 5);
    }
    map.MakeTable("11:0; 0-10:0"); // Existing syntax defaults to both states.
    assert(map.GetEnabledTriggerMask(SpillGate::Off) == 0x801);
    map.MakeTable("0 | 1");
    assert(map.GetEnabledTriggerMask(SpillGate::On) == 1);
    assert(map.GetEnabledTriggerMask(SpillGate::Off) == 1);

    Trigger trigger;
    trigger.SetTimeRegion(4);
    trigger.SetSubTimeRegion(4, {});
    std::string config = "0-2-1:0; 1-3-2:0; 2-1-3:0; 3-1-0:0";
    trigger.MakeTable(config);
    const auto run = [&](SpillGate gate) {
        trigger.CleanUpTimeRegion();
        trigger.GetTimeRegion()[1] = 1;
        trigger.GetTimeRegion()[3] = 1;
        trigger.SetSpillGate(gate);
        return *trigger.Scan();
    };
    assert((run(SpillGate::On) == std::vector<uint32_t>{1, 3}));
    assert((*trigger.GetHitTriggerFlags() == std::vector<uint32_t>{0x4005, 0x5005}));
    assert((run(SpillGate::Off) == std::vector<uint32_t>{1, 3}));
    assert((*trigger.GetHitTriggerFlags() == std::vector<uint32_t>{0x4006, 0x4006}));
    assert(trigger.GetBeforeCounts()[0] == 2); // OFF did not advance ON ID.
    assert((run(SpillGate::Off) == std::vector<uint32_t>{1, 3}));
    assert((*trigger.GetHitTriggerFlags() == std::vector<uint32_t>{0x6006, 0x4006}));
    run(SpillGate::Unknown);
    assert((*trigger.GetHitTriggerFlags() == std::vector<uint32_t>{0x4005, 0x5005}));
    assert(trigger.GetBeforeCounts()[3] == 0 && trigger.GetAfterCounts()[3] == 0);
    trigger.ResetPrescaleCounters();
    assert(trigger.GetBeforeCounts()[0] == 0);
    config = "0-0-1:0; 1-1-0:0";
    trigger.MakeTable(config);
    assert(run(SpillGate::On).empty());
    assert(trigger.GetBeforeCounts()[0] == 2 && trigger.GetAfterCounts()[0] == 0);

    SpillGateEvidence evidence;
    assert(evidence.State() == SpillGate::Unknown);
    evidence.AddWord(hbd(100, 1), 5, 0, true);
    evidence.AddWord(hbd(100, 1), 6, 0, true);
    assert(evidence.State() == SpillGate::On);
    evidence.AddWord(hbd(100, 0), 5, 0, true);
    assert(evidence.State() == SpillGate::Unknown && evidence.Conflict());
    SpillGateEvidence mismatch;
    mismatch.AddWord(hbd(100, 1), 5, 0, true);
    mismatch.AddWord(hbd(101, 1), 5, 0, true);
    assert(mismatch.State() == SpillGate::Unknown && mismatch.Conflict());
    SpillGateEvidence off, inverted, unsupported, disabled;
    off.AddWord(hbd(100, 0), 6, 0, true);
    inverted.AddWord(hbd(100, 0), 6, 0, false);
    unsupported.AddWord(hbd(100, 1), 2, 0, true);
    disabled.AddWord(hbd(100, 1), 5, -1, true);
    assert(off.State() == SpillGate::Off);
    assert(inverted.State() == SpillGate::On);
    assert(unsupported.State() == SpillGate::Unknown);
    assert(disabled.State() == SpillGate::Unknown);
    uint64_t split[] = {hbd(7, 1), 0x1eULL << 58};
    uint64_t packed[] = {HeartbeatFrame::MAGIC, 0, 0xbULL << 58, hbd(7, 1), 0x1eULL << 58};
    SpillGateEvidence splitGate, packedGate, shortGate;
    splitGate.AddPart(split, sizeof(split), 5, 0, true);
    packedGate.AddPart(packed, sizeof(packed), 6, 0, true);
    shortGate.AddPart(split, 7, 5, 0, true);
    assert(splitGate.State() == SpillGate::On && packedGate.State() == SpillGate::On);
    assert(shortGate.State() == SpillGate::Unknown);
    std::cout << "PASS spill gate parser, masks, counters, phases, flags and HBD evidence\n";
}
