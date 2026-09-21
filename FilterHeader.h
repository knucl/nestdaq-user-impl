#ifndef FilterHeader_h
#define FilterHeader_h

#include <cstdint>
#include <sys/time.h>

namespace Filter {
namespace v0 {
// "NIOC-TLF" : little endian of "FLT-COIN"
constexpr uint64_t MAGIC {0x4e494f43'2d544c46};

struct Header {
    uint64_t magic       {MAGIC};
    uint64_t length      {0};
    uint32_t numTrigs    {0};
    uint32_t workerId    {0};
    uint32_t elapseTime  {0};
    //struct timeval processTime {0, 0};
    uint64_t timeSec     {0};
    uint64_t timeUSec    {0};
};
} //namespace v0

inline namespace v1 {
#pragma pack(4)

// " IGOLTLF" : little endian of "FLTLOGI "
static constexpr uint64_t MAGIC {0x0049474f'4c544c46};

struct Header {
    uint64_t magic       {MAGIC};
    uint32_t length      {0};
    uint16_t hLength     {56};
    uint16_t type        {0};
    uint32_t timeFrameId {0};
    uint32_t numTrigs    {0};
    uint32_t workerId    {0};
    uint32_t numMessages {0};
    uint32_t elapseTime  {0};
    uint32_t reserve     {0xff00ff00};
    //struct timeval processTime {0, 0};
    uint64_t timeSec     {0};
    uint64_t timeUSec    {0};
};

constexpr unsigned char FLT_TDC_TYPE {0xaa};
constexpr uint32_t TRG_TIME_RECORD_TYPE_MASK {0xff000000};
constexpr uint32_t TRG_TIME_OR_FLAGS_MASK   {0x00ffffff};
constexpr uint32_t TRG_TIME_RECORD_TYPE {
    static_cast<uint32_t>(FLT_TDC_TYPE) << 24
};

constexpr uint32_t MakeTrgTimeType(uint32_t orFlags)
{
    return TRG_TIME_RECORD_TYPE | (orFlags & TRG_TIME_OR_FLAGS_MASK);
}

constexpr uint32_t GetTrgTimeRecordType(uint32_t type)
{
    return type & TRG_TIME_RECORD_TYPE_MASK;
}

constexpr uint32_t GetTrgTimeOrFlags(uint32_t type)
{
    return type & TRG_TIME_OR_FLAGS_MASK;
}

// A configured type without OR flags matches every flag combination of the
// same record type. This keeps existing monitors configured with 0xaa000000
// compatible with flagged trigger records.
constexpr bool MatchesTrgTimeType(uint32_t actual, uint32_t configured)
{
    return GetTrgTimeRecordType(actual) == GetTrgTimeRecordType(configured)
        && ((GetTrgTimeOrFlags(configured) == 0) || (actual == configured));
}

struct TrgTime {
    union {
        struct {
            uint32_t time;
            uint32_t type;
        };
        unsigned char cdata[8]  {0, 0, 0, 0, 0, 0, 0, FLT_TDC_TYPE};
    };
};

// " EMITGRT"
static constexpr uint64_t TDC_MAGIC {0x00454d49'54475254};
struct TrgTimeHeader {
    union {
        uint32_t u32data[4];
        struct {
            uint64_t magic       {TDC_MAGIC};
            uint32_t length      {0};
            uint16_t hLength     {16};
            uint16_t type        {0};
            //uint32_t timeFrameId {0};
        };
    };
};

// " TLFHGIH" : little endian of "HIGHFLT "
static constexpr uint64_t HIGH_MAGIC {0x20544c46'48474948};
struct HighLevelFilterHeader {
    uint64_t magic    {HIGH_MAGIC};
    uint32_t length   {0};
    uint16_t hLength  {24};
    uint16_t numTrigs {0};
    TrgTime  trgTime  {0};
};

struct HighLevelFilter {
    uint32_t time;
    uint32_t type;
};

#pragma pack()
} //namespace v1

} // namespace Filter

#endif
