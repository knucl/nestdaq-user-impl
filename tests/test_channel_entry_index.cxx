// g++ -std=c++17 -O2 -I. tests/test_channel_entry_index.cxx -o /tmp/test_channel_index
#include "Trigger.cxx"
#include <cassert>
#include <iostream>

int main()
{
    Trigger trigger;
    trigger.SetTimeRegion(256);
    trigger.SetMarkLen(0);
    // Same channel registered twice with different groups and offsets.
    trigger.EntryTo(0, UINT32_MAX, UINT32_MAX, 101, 7, 3);
    trigger.EntryTo(3, UINT32_MAX, UINT32_MAX, 101, 7, 7);
    trigger.BuildChannelEntryIndex();
    trigger.BuildChannelEntryIndex(); // Rebuild must not duplicate entries.
    trigger.CleanUpTimeRegion();
    uint64_t words[] = {
        (0xbULL << 58) | (7ULL << 51) | (100ULL << 12),
        (0xbULL << 58) | (8ULL << 51) | (100ULL << 12), // unregistered
        (0x1cULL << 58) // not a TDC hit
    };
    trigger.Mark(reinterpret_cast<unsigned char*>(words), sizeof(words), 101, 5);
    trigger.Mark(reinterpret_cast<unsigned char*>(words), sizeof(words), 999, 5);
    for (unsigned t = 0; t < 256; ++t)
        assert(trigger.GetTimeRegion()[t] == (t == 103 ? 1u : t == 107 ? 8u : 0u));

    // Clear and re-register another FE and highest supported channel number.
    trigger.ClearEntry();
    trigger.BuildChannelEntryIndex();
    trigger.EntryTo(2, UINT32_MAX, UINT32_MAX, 102, 255, 0);
    trigger.BuildChannelEntryIndex();
    trigger.CleanUpTimeRegion();
    trigger.Mark(reinterpret_cast<unsigned char*>(words), sizeof(words), 101, 5);
    uint64_t high = (0xbULL << 58) | (255ULL << 50) | (100ULL << 17);
    trigger.Mark(reinterpret_cast<unsigned char*>(&high), sizeof(high), 102, 6);
    for (unsigned t = 0; t < 256; ++t)
        assert(trigger.GetTimeRegion()[t] == (t == 100 ? 4u : 0u));

    trigger.ClearEntry();
    trigger.EntryTo(0, UINT32_MAX, UINT32_MAX, 101, 256, 0);
    bool rejected = false;
    try { trigger.BuildChannelEntryIndex(); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "PASS duplicate registrations, unregistered FE/ch, rebuild, clear/re-register, ch255 and invalid ch\n";
}
