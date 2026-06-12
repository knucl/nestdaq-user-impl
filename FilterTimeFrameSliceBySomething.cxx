/*
 * @file FilterTimeFrameSliceBySomething
 * @brief Slice Timeframe by Logic timing for NestDAQ
 * @date Created : 2024-05-04 12:31:55 JST
 *       Last Modified : 2024-05-23 07:12:22 JST
 *
 * @author Shinsuke OTA <ota@rcnp.osaka-u.ac.jp>
 *
 */
#include "FilterTimeFrameSliceBySomething.h"
#include "FilterTimeFrameSliceABC.icxx"
#include "fairmq/runDevice.h"

#include "utility/MessageUtil.h"
#include "UnpackTdc.h"

#define DEBUG 0

using nestdaq::FilterTimeFrameSliceBySomething;
namespace bpo = boost::program_options;




FilterTimeFrameSliceBySomething::FilterTimeFrameSliceBySomething()
{
}

bool FilterTimeFrameSliceBySomething::ProcessSlice(TTF& tf)
{
  for (const auto& stf : tf) {
    auto hr_tdcs = std::vector<TDC64H::tdc64>();
    auto lr_tdcs = std::vector<TDC64L::tdc64>();
    auto header = stf->GetHeader();
    auto ip = header->femId;
    auto& hbf = stf->at(0);
    auto n_data = hbf->GetNumData();
    LOG(info) << "nData: " << n_data;
    for (int i = 0; i < n_data; ++i) {
      auto fem_type = header->femType;
      if (fem_type == SubTimeFrame::TDC64H_V3) {
        hr_tdcs.emplace_back();
        auto flag = TDC64H::Unpack(hbf->UncheckedAt(i), &hr_tdcs.back());
        if (flag != TDC64H::T_TDC) {
          hr_tdcs.pop_back();
        }
      } else if (fem_type == SubTimeFrame::TDC64L_V3) {
        lr_tdcs.emplace_back();
        auto flag = TDC64L::Unpack(hbf->UncheckedAt(i), &lr_tdcs.back());
        if (flag != TDC64L::T_TDC) {
          lr_tdcs.pop_back();
        }
      }
    }
for (const auto& tdc : hr_tdcs) {
  LOG(INFO) << "HR-TDC ch: " << tdc.ch << ", tot: " << tdc.tot;
}
for (const auto& tdc : lr_tdcs) {
  LOG(INFO) << "LR-TDC ch: " << tdc.ch << ", tot: " << tdc.tot;
}
  }

  return true;
}



////////////////////////////////////////////////////
// override runDevice
////////////////////////////////////////////////////

void addCustomOptions(bpo::options_description& options)
{
   using opt = FilterTimeFrameSliceBySomething::OptionKey;

   options.add_options()
      (opt::InputChannelName.data(),
       bpo::value<std::string>()->default_value("in"),
       "Name of the input channel")
      (opt::OutputChannelName.data(),
       bpo::value<std::string>()->default_value("out"),
       "Name of the output channel")
      (opt::DQMChannelName.data(),
       bpo::value<std::string>()->default_value("dqm"),
       "Name of the data quality monitoring channel")
      (opt::PollTimeout.data(),
       bpo::value<std::string>()->default_value("1"),
       "Timeout of polling (in msec)")
      (opt::SplitMethod.data(),
       bpo::value<std::string>()->default_value("1"),
       "STF split method")
      ;
   
}



std::unique_ptr<fair::mq::Device> getDevice(fair::mq::ProgOptions& /*config*/)
{
    return std::make_unique<FilterTimeFrameSliceBySomething>();
}

