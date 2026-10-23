// Standalone regression test. Compile with -std=c++17 and -I<repository>.
#include "Trigger.cxx"
#include <iostream>

static uint64_t hitWord(uint32_t type, uint32_t ch, uint32_t tick)
{
    const unsigned chShift = type == 6 ? 50 : 51;
    const unsigned timeShift = type == 3 ? 18 : type == 6 ? 17 : 12;
    return (uint64_t{0xb} << 58) | (uint64_t{ch} << chShift)
        | (uint64_t{tick} << timeShift);
}

int main()
{
    unsigned cases = 0;
    for (uint32_t type : {2u, 3u, 5u, 6u})
    for (int width : {0, 1, 2, 10})
    for (int offset : {-3, 0, 4})
    for (uint32_t tick : {0u, 2u, 127u, 250u, 255u}) {
        Trigger trigger;
        trigger.SetTimeRegion(256);
        trigger.SetMarkLen(width);
        trigger.EntryTo(0, UINT32_MAX, UINT32_MAX, 101, 0, offset);
        trigger.EntryTo(1, 0, 0, 101, 0, offset);
        trigger.EntryTo(1, 0, 0, 101, 1, offset);
        trigger.BuildChannelEntryIndex();
        trigger.SetSubTimeRegion(256, {{0, 2}});
        trigger.CleanUpTimeRegion();
        uint64_t words[] = {hitWord(type, 0, tick), hitWord(type, 1, tick)};
        trigger.Mark(reinterpret_cast<unsigned char*>(words), sizeof(words), 101, type);
        trigger.ScanSubTRandMarkMainTR();
        const uint32_t hit = tick + offset;
        const uint32_t half = width / 2;
        for (uint32_t bin = 0; bin < 256; ++bin) {
            bool expected = false;
            if (hit < 256 - half) {
                for (int k = -static_cast<int>(half); k <= static_cast<int>(half); ++k)
                    if (hit + k == bin) expected = true;
            }
            const auto actual = trigger.GetTimeRegion()[bin];
            if (actual != (expected ? 3u : 0u)) {
                std::cerr << "FAIL type=" << type << " width=" << width
                    << " offset=" << offset << " tick=" << tick << " bin=" << bin
                    << " actual=" << actual << " expected=" << (expected ? 3u : 0u) << '\n';
                return 1;
            }
        }
        ++cases;
    }
    std::cout << "PASS " << cases << " cases: four TDC formats, windows, offsets, HBF edges, subgroup AND\n";
}
