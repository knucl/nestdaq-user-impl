/*
 *
 *
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <array>
#include <algorithm>
#include <stdexcept>

#include <string.h>
#include <assert.h>

#include <bitset>

#include "UnpackTdc.h"
#include "SubTimeFrameHeader.h"
#include "FilterHeader.h"
#include "TriggerMap.cxx"
#include "SignalParser.h"
#include "LogicFilterTiming.h"

#define DEBUG_MORE32 0


struct HBFIndex {
	int msg_index;
	uint32_t timeFrameId;
	uint32_t femType;
	uint32_t femId;
	unsigned char* data;
	int size;
};


class Trigger
{
public:
	Trigger();
	virtual ~Trigger();
	void InitParam();
	bool SetTimeRegion(int);
	bool SetSubTimeRegion(int, const std::map<uint32_t, uint32_t>&); // subTimeRegionSize, nEntryInSubTimeRegion
	void CleanUpTimeRegion();
	void CleanUpSubTimeRegion(const std::map<uint32_t, uint32_t>& nEntryInSubTimeRegion);
	uint32_t *GetTimeRegion();
	uint32_t GetTimeRegionSize();
	std::vector<uint32_t> *GetHitTriggerFlags();
	void Entry(uint32_t, int, int); // fem, ch, offset
	void Entry(uint32_t, int, int, uint32_t, uint32_t); // fem, ch, offset, leftwidth, rightwidth
	void EntryTo(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int); // group_id, subgroup_id, iSubTimeRegion, fem, ch, offset
	void EntryTo(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int, uint32_t, uint32_t); // group_id, subgroup_id, iSubTimeRegion, fem, ch, offset, leftwidth, rightwidth
	void ClearEntry();
	void BuildChannelEntryIndex();
	bool CheckEntryFEM(uint32_t);
	void Mark(unsigned char *, int, int, uint32_t);
	std::vector<uint32_t> *Scan(LogicFilterTiming::Frame* timing = nullptr);
	void ScanSubTRandMarkMainTR();
	std::vector<uint32_t> *Exec(std::vector<struct HBFIndex> &, LogicFilterTiming::Frame* timing = nullptr);
	void SetMarkLen(int val) {fMarkLen = val;};
	int GetMarkLen() {return fMarkLen;};
	//void SetLogic(int);
	void MakeTable(std::string &);
	void ResetPrescaleCounters() { fTMap.ResetPrescaleCounters(); }
	const std::array<uint64_t, 12>& GetBeforeCounts() const { return fTMap.GetBeforeCounts(); }
	const std::array<uint64_t, 12>& GetAfterCounts() const { return fTMap.GetAfterCounts(); }
	void SetfnEntryInSubTimeRegion(const std::map<uint32_t, uint32_t>& nEntryInSubTimeRegion);
protected:
private:
	using ChannelEntryTable = std::array<std::vector<std::size_t>, 256>;
	// FE ID -> channel -> indices in the existing registration vectors.
	std::map<uint32_t, ChannelEntryTable> fEntryIndicesByChannel;
	//std::vector<struct CoinCh> fEntry;
	std::map< uint32_t, std::vector<int> > fEntryCh;
	std::map< uint32_t, std::vector<int> > fEntryChDelay;
	std::map< uint32_t, std::vector<uint32_t> > fEntryChBit;
	std::map< uint32_t, std::vector<uint32_t> > fEntryChLeftWidth; // [T - leftwidth, T + rightwidth]
	std::map< uint32_t, std::vector<uint32_t> > fEntryChRightWidth; // [T - leftwidth, T + rightwidth]
	std::map<std::pair<uint32_t, uint32_t>, int> fEntryCounts; // key: (gp_id, subgp_id), value: count of entry
	std::map< uint32_t, std::vector<uint32_t> > fEntryChiSubTimeRegion; // key: fem, value: list of iSubTimeRegion
	std::map< uint32_t, uint32_t > fiSubTimeRegionMainTimeRegion; // key: iSubTimeRegion, value: iMainTimeRegion

	std::map< uint32_t, uint32_t> fNEntryInSubTimeRegion_Trigger; // key: iSubTimeRegion, value: nEntryInSubTimeRegion

	int fEntryCounts_old = 0;
	uint32_t fEntryMask = 0;

	int fNentry = 0;
	uint32_t fTimeRegionSize = 0; // size of main TimeRegion
	uint32_t *fTimeRegion = nullptr; // main TimeRegion
	uint32_t fSubTimeRegionSize = 0; // number of time bins, not uint64_t words
//	std::vector<uint32_t*> fSubTimeRegions; // vector of SubTimeRegions.
	std::vector<std::array<std::vector<uint64_t>, 2>> fSubTimeRegions;
	//int fMarkCount = 0;
	//uint32_t fMarkMask = 0;
	std::vector<uint32_t> fHits;
	std::vector<uint32_t> fHitTriggerFlags;
	int fMarkLen = 5;

	TriggerMap fTMap;
};

Trigger::Trigger()
{
	return;
}

Trigger::~Trigger()
{
	if (fTimeRegion != nullptr) {
		delete[] fTimeRegion;
		fTimeRegion = nullptr;
	}

	return;
}

void Trigger::SetfnEntryInSubTimeRegion(const std::map<uint32_t, uint32_t>& fNEntryInSubTimeRegion_LogicFilter){
	fNEntryInSubTimeRegion_Trigger = fNEntryInSubTimeRegion_LogicFilter;
}

//void Trigger::SetLogic(int nsignal, std::string formula)
void Trigger::MakeTable(std::string & formula)
{
	fTMap.MakeTable(formula);
	return;
}

void Trigger::InitParam()
{
	//fMarkCount = 0;
	//fMarkMask = 0;
	fHits.clear();
	fHits.resize(0);
	fHitTriggerFlags.clear();
	fHitTriggerFlags.resize(0);
	if (fTimeRegion != nullptr) {
		memset(fTimeRegion, 0, fTimeRegionSize * sizeof(uint32_t));
	}

	return;
}

bool Trigger::SetTimeRegion(int size)
{
	if (size <= 0) throw std::invalid_argument("TimeRegion size must be positive");
	fTimeRegionSize = size;
	if (fTimeRegion != nullptr) {
		delete[] fTimeRegion;
		fTimeRegion = nullptr;
	}
	fTimeRegion = new uint32_t[size];

	return true;
}

bool Trigger::SetSubTimeRegion(int subTimeRegionSize, const std::map<uint32_t, uint32_t>& nEntryInSubTimeRegion){
	if (subTimeRegionSize <= 0 ||
	    static_cast<uint32_t>(subTimeRegionSize) != fTimeRegionSize) {
		throw std::invalid_argument("SubTimeRegion size must match positive TimeRegion size");
	}
	for (const auto& p : nEntryInSubTimeRegion) {
		if (p.second != 2 || p.first >= nEntryInSubTimeRegion.size()) {
			throw std::invalid_argument("SubTimeRegions require exactly 2 inputs and contiguous indices");
		}
	}
	fSubTimeRegionSize = static_cast<uint32_t>(subTimeRegionSize);
	const std::size_t wordCount = (static_cast<std::size_t>(fSubTimeRegionSize) + 63) / 64;
	fSubTimeRegions.resize(nEntryInSubTimeRegion.size());
	for (auto& region : fSubTimeRegions) {
		region[0].assign(wordCount, uint64_t{0});
		region[1].assign(wordCount, uint64_t{0});
	}

	return true;

} // bool Trigger::SetSubTimeRegion(int subTimeRegionSize, const std::map<uint32_t, uint32_t>& nEntryInSubTimeRegion)

void Trigger::CleanUpTimeRegion()
{
	for (uint32_t i = 0 ; i < fTimeRegionSize ; i++) fTimeRegion[i] = 0;

	return;
}

void Trigger::CleanUpSubTimeRegion(const std::map<uint32_t, uint32_t>& nEntryInSubTimeRegion)
{
	(void)nEntryInSubTimeRegion; // Retain the existing public reset interface.
	for (auto& region : fSubTimeRegions) {
		std::fill(region[0].begin(), region[0].end(), uint64_t{0});
		std::fill(region[1].begin(), region[1].end(), uint64_t{0});
	}
	return;
}

uint32_t *Trigger::GetTimeRegion()
{
	return fTimeRegion;
}

uint32_t Trigger::GetTimeRegionSize()
{
	return fTimeRegionSize;
}

std::vector<uint32_t> *Trigger::GetHitTriggerFlags()
{
	return &fHitTriggerFlags;
}


//if (Trig::CheckEntryFEM(lsubtimeframe.femId)) Trig::Mark(pdata);

#if 0
void Trigger::Entry(uint64_t fem, int ch)
{
	bool match = false;
	for (auto ent ; fEntry) {
		if (ent.femId == fem) {
			fEntry.Ch.emplace_back(ch);
			match = true;
			fNentry++;
		}
	} 
	if (match) {
		struct CoinCh newentry;
		newentry.fenId = fem;
		newentry.Ch.emplace_back(ch);
		fEntry.emplace_back(newentry);
		fNentry++;
	}

	return;
}

bool Trigger::CheckEntryFEM(uint64_t fem)
{
	bool rval = false;
	for (auto ent ; fEntry) if (ent.femId == fem) rval = true;
	return rval;
}

#endif

void Trigger::Entry(uint32_t fem, int ch, int offset)
{
	fEntryIndicesByChannel.clear();
	fEntryCh[fem].emplace_back(ch);
	fEntryChDelay[fem].emplace_back(offset);
	fEntryChBit[fem].emplace_back(0x0000001 << fEntryCounts_old);
	fEntryChLeftWidth[fem].emplace_back(fMarkLen / 2); // if mq-param give a signal(fem, ch, offset) with 3 parameters, leftwidth and rightwidth are set to default value (MarkLen / 2)
	fEntryChRightWidth[fem].emplace_back(fMarkLen / 2); // if mq-param give a signal(fem, ch, offset) with 3 parameters, leftwidth and rightwidth are set to default value (MarkLen / 2)
	fEntryMask |= 0x00000001 << fEntryCounts_old;
	fEntryCounts_old++;

	#if 0
	std::cout << "#D Trig Entry : Module: " << fem << " Ch: " << ch << std::endl;
	#endif
	if (static_cast<unsigned int>(fEntryCounts_old) > (sizeof(uint32_t) * 8)) {
		std::cerr << "Entry Ch. exceed " << sizeof(uint32_t) * 8<< std::endl;
	}
	assert(fEntryCounts_old <= static_cast<int>(sizeof(uint32_t) * 8));

	return;
}

void Trigger::EntryTo(uint32_t group_id, uint32_t subgroup_id, uint32_t iSubTimeRegion, uint32_t fem, uint32_t ch, int offset)
{
	if (group_id >= 32) throw std::invalid_argument("group_id must be less than 32");
	if (subgroup_id != SignalParser::NO_SUBGROUP &&
	    fEntryCounts[std::make_pair(group_id, subgroup_id)] >= 2) {
		throw std::invalid_argument("Each subgroup supports exactly 2 inputs; third input rejected");
	}
	fEntryIndicesByChannel.clear(); // Registrations changed; rebuild before Mark.
	fEntryCh[fem].emplace_back(ch);
	fEntryChDelay[fem].emplace_back(offset);
	if(subgroup_id == SignalParser::NO_SUBGROUP){
		fEntryChBit[fem].emplace_back(uint32_t(1u) << group_id);
	}
	else{
		fEntryChBit[fem].emplace_back(uint32_t(1u) << fEntryCounts[std::make_pair(group_id, subgroup_id)]++);
	}

	fEntryChLeftWidth[fem].emplace_back(fMarkLen / 2);
	fEntryChRightWidth[fem].emplace_back(fMarkLen / 2);
	
	if(subgroup_id == SignalParser::NO_SUBGROUP){
		fEntryChiSubTimeRegion[fem].emplace_back(UINT32_MAX); // なにか入れておかないと他のエントリーとずれてしまうので、とりあえず。
	}
	else{
		fEntryChiSubTimeRegion[fem].emplace_back(iSubTimeRegion);
	}

	if(iSubTimeRegion != UINT32_MAX) fiSubTimeRegionMainTimeRegion[iSubTimeRegion] = uint32_t(1u) << group_id;
	if(subgroup_id == SignalParser::NO_SUBGROUP){
		std::cout << "[Trigger::EntryTo] This signal (group_id=" << group_id << ", subgroup_id=NO_SUBGROUP) is assigned to MainTimeRegion bit " << std::bitset<32>(fEntryChBit[fem].back()) << std::endl;
	}
	else{
		std::cout << "[Trigger::EntryTo] This signal (group_id=" << group_id << ", subgroup_id=" << subgroup_id << ") is assigned to SubTimeRegion " << iSubTimeRegion << " and its bit " << std::bitset<32>(fEntryChBit[fem].back()) << ". And to be assigned to MainTimeRegion bit " << std::bitset<32>(fiSubTimeRegionMainTimeRegion[iSubTimeRegion]) << std::endl;
	}

	fEntryMask |= uint32_t(1u) << group_id;
	
	return;
}

// void Trigger::Entry(uint32_t fem, int ch, int offset, uint32_t leftwidth, uint32_t rightwidth)
// {

// 	fEntryCh[fem].emplace_back(ch);
// 	fEntryChDelay[fem].emplace_back(offset);
// 	fEntryChBit[fem].emplace_back(0x0000001 << fEntryCounts);
// 	fEntryChLeftWidth[fem].emplace_back(leftwidth);
// 	fEntryChRightWidth[fem].emplace_back(rightwidth);
// 	fEntryMask |= 0x00000001 << fEntryCounts;
// 	fEntryCounts++;

// 	#if 0
// 	std::cout << "#D Trig Entry : Module: " << fem << " Ch: " << ch << std::endl;
// 	#endif
// 	if (static_cast<unsigned int>(fEntryCounts) > (sizeof(uint32_t) * 8)) {
// 		std::cerr << "Entry Ch. exceed " << sizeof(uint32_t) * 8<< std::endl;
// 	}
// 	assert(fEntryCounts <= static_cast<int>(sizeof(uint32_t) * 8));

// 	return;
// }

void Trigger::EntryTo(uint32_t group_id, uint32_t subgroup_id, uint32_t iSubTimeRegion, uint32_t fem, uint32_t ch, int offset, uint32_t leftwidth, uint32_t rightwidth)
{
	// to be implemented later
	return;
}

void Trigger::ClearEntry()
{
	fEntryIndicesByChannel.clear();
	fEntryCh.clear();
	fEntryChDelay.clear();
	fEntryChBit.clear();
	fEntryMask = 0x00000000;
	fEntryCounts.clear();
	fEntryChLeftWidth.clear();
	fEntryChRightWidth.clear();
	fEntryChiSubTimeRegion.clear();

	return;
}

void Trigger::BuildChannelEntryIndex()
{
    std::map<uint32_t, ChannelEntryTable> tables;
    for (const auto& entry : fEntryCh) {
        const uint32_t fem = entry.first;
        const auto& channels = entry.second;
        auto& table = tables[fem];
        for (std::size_t i = 0; i < channels.size(); ++i) {
            const int ch = channels[i];
            if (ch < 0 || static_cast<std::size_t>(ch) >= table.size()) {
                throw std::invalid_argument("Channel number must be 0..255");
            }
            table[ch].push_back(i); // Preserve duplicate registrations and order.
        }
    }
    fEntryIndicesByChannel.swap(tables);
}

bool Trigger::CheckEntryFEM(uint32_t fem)
{
	bool rval = false;
	if (fEntryCh.count(fem) >= 1) rval = true;
	return rval;
}

void Trigger::Mark(unsigned char *pdata, int len, int fem, uint32_t type)
{
    // Decode once per word and visit only registrations for the decoded channel.
    const auto entry = fEntryCh.find(fem);
    if (entry == fEntryCh.end()) return;
    const auto& channels = entry->second;
    const auto& table = fEntryIndicesByChannel.at(fem);
    const auto& delays = fEntryChDelay.at(fem);
    const auto& leftWidths = fEntryChLeftWidth.at(fem);
    const auto& rightWidths = fEntryChRightWidth.at(fem);
    const auto& markBits = fEntryChBit.at(fem);
    const auto& subgroups = fEntryChiSubTimeRegion.at(fem);
    const auto* words = reinterpret_cast<const uint64_t*>(pdata);

    const auto markWords = [&](auto unpack, auto tdc, int hitType, bool useRightBoundary) {
        for (std::size_t j = 0; j < len / sizeof(uint64_t); ++j) {
            if (unpack(words[j], &tdc) != hitType) continue;
            if (tdc.ch < 0 || static_cast<std::size_t>(tdc.ch) >= table.size()) continue;
            for (std::size_t i : table[tdc.ch]) {
                const uint32_t leftwidth = leftWidths[i];
                const uint32_t rightwidth = rightWidths[i];
                const uint32_t markbit = markBits[i];
                const uint32_t subgroup = subgroups[i];
                const uint32_t hit = tdc.tdc4n + delays[i];
                // Preserve the old format-specific boundary checks.
                const uint32_t boundary = useRightBoundary ? rightwidth : leftwidth;
                if (hit >= fTimeRegionSize - boundary) continue;
                uint64_t* subBits = subgroup != UINT32_MAX
                    ? fSubTimeRegions[subgroup][markbit == 1u ? 0u : 1u].data() : nullptr;
                for (int k = -static_cast<int>(leftwidth);
                     k < static_cast<int>(rightwidth) + 1; ++k) {
                    const uint32_t t = hit + k;
                    if (t < fTimeRegionSize) {
                        if (subBits) subBits[t / 64] |= uint64_t{1} << (t % 64);
                        else fTimeRegion[t] |= markbit;
                    } else if (static_cast<int>(hit) + k >= 0) {
                        std::cout << "#E Over range hit!"
                            << " FEM: " << std::hex << fem
                            << " Ch: " << std::dec << channels[i]
                            << " Hit: " << hit << std::endl;
                    }
                }
            }
        }
    };

    if (type == SubTimeFrame::TDC64H) {
        markWords([](uint64_t w, TDC64H::tdc64* t) { return TDC64H::Unpack(w, t); },
                  TDC64H::tdc64{}, TDC64H::T_TDC, false);
    } else if (type == SubTimeFrame::TDC64L) {
        markWords([](uint64_t w, TDC64L::tdc64* t) { return TDC64L::Unpack(w, t); },
                  TDC64L::tdc64{}, TDC64L::T_TDC, false);
    } else if (type == SubTimeFrame::TDC64H_V3) {
        markWords([](uint64_t w, TDC64H_V3::tdc64* t) { return TDC64H_V3::Unpack(w, t); },
                  TDC64H_V3::tdc64{}, TDC64H_V3::T_TDC, true);
    } else if (type == SubTimeFrame::TDC64L_V3) {
        markWords([](uint64_t w, TDC64L_V3::tdc64* t) { return TDC64L_V3::Unpack(w, t); },
                  TDC64L_V3::tdc64{}, TDC64L_V3::T_TDC, false);
    }
}

std::vector<uint32_t> *Trigger::Scan(LogicFilterTiming::Frame* timing)
{
	#if DEBUG_MORE32
	std::cout << "[Trigger::Scan] Start scanning SubTimeRegion and marking MainTimeRegion..." << std::endl;
	#endif
	// --------------------------------
	// Process AND logic
	// --------------------------------
	{
		LogicFilterTiming::Scope timer(timing, LogicFilterTiming::SubAnd);
		Trigger::ScanSubTRandMarkMainTR();
	}
	// --------------------------------
	// Finish processing AND logic, and marking MainTimeRegion
	// --------------------------------
	#if DEBUG_MORE32
	std::cout << "\tDone." << std::endl;
	#endif

	#if DEBUG_MORE32
	std::cout << "[Trigger::Scan] Start scanning MainTimeRegion..." << std::endl;
	#endif
	//std::cout << "#D Scan fMarkMask: " << std::hex << fMarkMask << std::endl;
	//std::cout << "#D Scan fEntryMask: " << std::hex << fEntryMask << std::endl;
	LogicFilterTiming::Scope lutTimer(timing, LogicFilterTiming::Lut);
	fHits.clear();
	fHits.resize(0);
	fHitTriggerFlags.clear();
	fHitTriggerFlags.resize(0);

	for (unsigned int i = 0 ; i < fTimeRegionSize - 1; i++) {
		#if 0
		if (((fEntryMask & fTimeRegion[i]) != fEntryMask)
		&& ((fEntryMask & fTimeRegion[i + 1]) == fEntryMask)) {
			fHits.emplace_back(i + 1);
		}
		#else
			const uint32_t prev = fTMap.LookUpTriggerFlags(fTimeRegion[i]);
			const uint32_t curr = fTMap.LookUpTriggerFlags(fTimeRegion[i + 1]);
			const uint32_t rawFlags = curr & ~prev;
			if (rawFlags != 0) {
				const uint32_t acceptedFlags = fTMap.ApplyPrescale(rawFlags);
				if (acceptedFlags != 0) {
					fHits.emplace_back(i + 1);
					// Keep before-prescale edges and actual acceptance separately.
					fHitTriggerFlags.emplace_back(
						Filter::MakeTrgTimePrescaleFlags(rawFlags, acceptedFlags));
				}
			}
		#endif

		#if 0
		if (fTimeRegion[i] != 0) {
			std::cout << "#D Scan Time: " << std::dec << i
				<< " Bits: " << std::hex << fTimeRegion[i]
				//<< " Mask: " << fMarkMask << std::endl;
				<< " Mask: " << fEntryMask << std::endl;
		}
		#endif
	}
	#if DEBUG_MORE32
	std::cout << "\tDone." << std::endl;
	#endif

	return &fHits;
} // std::vector<uint32_t> *Trigger::Scan()

void Trigger::ScanSubTRandMarkMainTR()
{
	#if DEBUG_MORE32
	std::cout << "[Trigger::ScanSubTRandMarkMainTR] size of fiSubTimeRegionMainTimeRegion: " << fiSubTimeRegionMainTimeRegion.size() << std::endl;
	#endif
	for(const auto &p : fiSubTimeRegionMainTimeRegion){
		uint32_t iSubTimeRegion = p.first; // index of SubTimeRegion
		uint32_t iMainTimeRegion = p.second; // a bit in MainTimeRegion reserved by this SubTimeRegion
    
		#if DEBUG_MORE32
		std::cout << "[Trigger::ScanSubTRandMarkMainTR] Start scanning SubTimeRegion: iSubTimeRegion: " << iSubTimeRegion << " iMainTimeRegion: " << std::bitset<32>(iMainTimeRegion) << std::endl;
		#endif

		auto& inputs = fSubTimeRegions[iSubTimeRegion];
		for (std::size_t word = 0; word < inputs[0].size(); ++word) {
			uint64_t coincidence = inputs[0][word] & inputs[1][word];
			// Reuse the loaded cache lines to reset both inputs for the next HBF.
			inputs[0][word] = 0;
			inputs[1][word] = 0;
			while (coincidence != 0) {
				const unsigned bit = static_cast<unsigned>(__builtin_ctzll(coincidence));
				const std::size_t t = word * 64 + bit;
				if (t < fSubTimeRegionSize) fTimeRegion[t] |= iMainTimeRegion;
				coincidence &= coincidence - 1;
			}
		}
		#if DEBUG_MORE32
		std::cout << "\tDone." << std::endl;
		#endif
	}
	return;
} // void Trigger::ScanSubTRandMarkMainTR()

std::vector<uint32_t> *Trigger::Exec(std::vector<struct HBFIndex> &hbf, LogicFilterTiming::Frame* timing)
{
	{
	LogicFilterTiming::Scope timer(timing, LogicFilterTiming::Mark);
	for (auto &seg : hbf) {
		//fTrig->Mark(
		//	reinterpret_cast<unsigned char *>(inParts[mindex].GetData()),
		//	inParts[mindex].GetSize(),
		//	vfemid, dbl->Type);
		Trigger::Mark(seg.data, seg.size, seg.femId, seg.femType);
	}
	}

	#if 0
	uint32_t *tr = fTrig->GetTimeRegion();
	std::cout << "####DDDD Hit TimeRegion: ";
	for (uint32_t ii = 0 ; ii < fTrig->GetTimeRegionSize() ; ii++) {
		if (tr[ii] != 0) {
		std::cout << " " << std::dec << i << ":"
			<< std::hex << std::setw(4) << std::setfill('0')
			<< tr[ii];
		}
	}
	std::cout << std::endl;
	#endif

	#if 0
	std::cout << "# HB: " << std::dec << i;
	for (size_t iifem = 0 ; iifem < block_map.size() ; iifem++) {
		struct DataBlock *dbl = &block_map[iifem][i];
		uint64_t vfemid = dbl->femId;
		uint64_t vhbframe = dbl->HBFrame;
		//std::cout << "# HB: " << std::dec << i
		//<< " FEM: " << std::hex << vfemid
		std::cout << " " << std::dec << (vfemid  & 0xff) << ":" << vhbframe;
	}
	std::cout << std::endl;
	#endif

	return Trigger::Scan(timing);
}




#ifdef TEST_MAIN_TRIG
int main(int argc, char* argv[])
{
	Trigger trig;

	static char cbuf[16];
	uint64_t *pdata = reinterpret_cast<uint64_t *>(cbuf);

	uint64_t *buf = new uint64_t[1024*1024*8];


	int time_region = 1024 * 256; //18 bit
	trig.SetTimeRegion(time_region);

	trig.Entry(1, 26, 10);
	trig.Entry(1, 29, 0);


	//struct TDC64H::tdc64 tdc;
	int i = 0;
	while (true) {
		std::cin.read(cbuf, 8);
		//std::cin >> *pdata; 
		if (std::cin.eof()) break;
		buf[i++] = *pdata;

		std::cout << "\r "  << i << ": " << *pdata << "  " << std::flush;
	
	}
	//int len = i * sizeof(uint64_t);
	std::cout << std::endl;

	unsigned char *pcurr = reinterpret_cast<unsigned char *>(buf);
	unsigned char *pend = reinterpret_cast<unsigned char *>(buf + i);
	unsigned char *pnext = nullptr;
	while (true) {
		int len = TDC64H::GetHBFrame(pcurr, pend, &pnext);
		if (len <= 0) break;

		std::cout << "#D HB frame size: " << std::dec << len
			<< " curr: " << std::hex
			<< reinterpret_cast<uintptr_t>(pcurr)
			<< " next: "
			<< reinterpret_cast<uintptr_t>(pnext) << std::endl;

		pdata = reinterpret_cast<uint64_t *>(pcurr);
		#if 0
		for (unsigned int j = 0 ; j < len / sizeof(uint64_t) ; j++) {
			tdc64h_dump(pdata[j]);
		}
		#endif

		trig.InitParam();
		trig.Mark(reinterpret_cast<unsigned char *>(pdata), len, 1, 0);
		std::vector<uint32_t> *nhits = trig.Scan();
		std::cout << "# Hits: ";
		for (auto hit : *nhits) std::cout << " " << hit;
		std::cout << std::endl;
		std::cout << "Nhits: " << std::dec << nhits->size()
			<< " / " << time_region << std::endl;




		pcurr = pnext;
	}


	#if 0
	trig.Mark(reinterpret_cast<unsigned char *>(buf), len, 1);
	std::vector<uint32_t> *nhits = trig.Scan();
	std::cout << "# ";
	for (auto hit : *nhits) std::cout << " " << hit;
	std::cout << std::endl;
	std::cout << "Nhits: " << std::dec << nhits->size()
		<< " / " << time_region << std::endl;
	#endif

	return 0;
}
#endif
