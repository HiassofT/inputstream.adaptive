/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Utils.h"

#include "Base64Utils.h"
#include "StringUtils.h"
#include "log.h"

#include <algorithm> // any_of
#include <chrono>
#include <cstring>
#include <stdio.h>

using namespace UTILS;
using namespace kodi::tools;

std::vector<uint8_t> UTILS::HvccToAnnexb(const std::vector<uint8_t>& hvcc)
{
  if (hvcc.size() < 23)
  {
    LOG::LogF(LOGERROR, "Cannot convert HVCC data, wrong data size");
    return {};
  }

  std::vector<uint8_t> result;
  const uint8_t* data = hvcc.data() + 22; // Start after length field
  const uint8_t* end = hvcc.data() + hvcc.size();
  uint8_t numSequences = *data++;

  for (uint8_t i = 0; i < numSequences; ++i)
  {
    if (data + 2 > end)
    {
      LOG::LogF(LOGERROR, "Cannot convert HVCC data, wrong data size");
      return {};
    }

    uint8_t nalType = (*data++ << 1);
    uint16_t numNals = (data[0] << 8) | data[1];
    data += 2;

    for (uint16_t j = 0; j < numNals; ++j)
    {
      if (data + 2 > end)
      {
        LOG::LogF(LOGERROR, "Cannot convert HVCC data, wrong data size");
        return {};
      }

      uint16_t nalSize = (data[0] << 8) | data[1];
      data += 2;

      if (data + nalSize > end)
      {
        LOG::LogF(LOGERROR, "Cannot convert HVCC data, wrong data size");
        return {};
      }

      // Add Annex B start code
      result.emplace_back(0x00);
      result.emplace_back(0x00);
      result.emplace_back(0x00);
      result.emplace_back(0x01);

      // Copy NAL data
      result.insert(result.end(), data, data + nalSize);
      data += nalSize;
    }
  }

  return result;
}

std::vector<uint8_t> UTILS::AnnexbToHvcc(const std::vector<uint8_t>& annexb)
{
  std::vector<uint8_t> result;

  size_t sz = annexb.size();
  if (sz <= 6 || annexb[0] != 0 || annexb[1] != 0 || annexb[2] != 0 || annexb[3] != 1)
  {
    return annexb;
  }

  const uint8_t* data = annexb.data() + 4;
  const uint8_t* nalPos[4] = {data, nullptr, nullptr, nullptr};
  const uint8_t* end = annexb.data() + sz;

  while (data + 4 <= end && (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1))
  {
    ++data;
  }
  nalPos[1] = data += 4;

  while (data + 4 <= end && (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1))
  {
    ++data;
  }
  nalPos[2] = data += 4;

  // Check that we are at the end
  while (data + 4 <= end && (data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1))
  {
    ++data;
  }

  if (data + 4 < end)
    return result;
  nalPos[3] = end + 4;

  // Check if we have expected information
  if (nalPos[0] < nalPos[1] && nalPos[1] < nalPos[2] && nalPos[2] < end && nalPos[0][0] == 0x40 &&
      nalPos[0][1] == 1 && nalPos[1][0] == 0x42 && nalPos[1][1] == 1 && nalPos[2][0] == 0x44 &&
      nalPos[2][1] == 1)
  {

    size_t sz = 22 + annexb.size() - 12 + 16;
    result.resize(sz, 0); // Unknown HVCC fields
    uint8_t* resultData = result.data() + 22;
    *resultData = 3, ++resultData; // numSequences

    for (unsigned int i = 0; i < 3; ++i)
    {
      *resultData = nalPos[i][0] >> 1, ++resultData; // Nalu type
      resultData[0] = 0, resultData[1] = 1, resultData += 2; // count nals
      uint16_t nalSz = static_cast<uint16_t>(nalPos[i + 1] - nalPos[i] - 4);
      resultData[0] = nalSz >> 8, resultData[1] = nalSz & 0xFF, resultData += 2; // count nals
      std::memcpy(resultData, nalPos[i], nalSz), resultData += nalSz;
    }
  }
  return result;
}

std::vector<uint8_t> UTILS::AnnexbToAvc(const std::vector<uint8_t>& annexb)
{
  std::vector<uint8_t> result;

  if (annexb.size() <= 6 || annexb[0] != 0 || annexb[1] != 0 || annexb[2] != 0 || annexb[3] != 1)
  {
    return annexb;
  }

  const uint8_t* sps = 0;
  const uint8_t* pps = 0;
  const uint8_t* end = annexb.data() + annexb.size();

  sps = pps = annexb.data() + 4;

  while (pps + 4 <= end && (pps[0] != 0 || pps[1] != 0 || pps[2] != 0 || pps[3] != 1))
  {
    ++pps;
  }

  //Make sure we have found pps start
  if (pps + 4 >= end)
    return result;

  pps += 4;

  result.resize(annexb.size() + 3); //need 3 byte more for new header
  size_t pos(0);

  result[pos++] = 1;
  result[pos++] = sps[1];
  result[pos++] = sps[2];
  result[pos++] = sps[3];
  result[pos++] = 0xFFU; //6 bits reserved(111111) + 2 bits nal size length - 1 (11)
  result[pos++] = 0xe1U; //3 bits reserved (111) + 5 bits number of sps (00001)

  size_t sz = pps - sps - 4;
  result[pos++] = static_cast<uint8_t>(sz >> 8);
  result[pos++] = static_cast<uint8_t>(sz & 0xFF);
  for (size_t i = 0; i < sz; ++i)
  {
    result[pos++] = sps[i];
  }

  result[pos++] = 1;
  sz = end - pps;
  result[pos++] = static_cast<uint8_t>(sz >> 8);
  result[pos++] = static_cast<uint8_t>(sz & 0xFF);
  for (size_t i = 0; i < sz; ++i)
  {
    result[pos++] = pps[i];
  }

  return result;
}

bool UTILS::IsAnnexB(const std::vector<uint8_t>& data)
{
  if (data.size() >= 4)
  {
    // Check for the 4-byte start code
    if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01)
      return true;

    // Check for the 3-byte start code
    if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01)
      return true;
  }
  return false;
}

std::vector<uint8_t> UTILS::AvcToAnnexb(const std::vector<uint8_t>& avc)
{
  if (avc.size() < 8)
    return {};

  // check if's already annexb, avc starts with 1
  if (avc[0] == 0)
    return avc;

  // calculate size
  std::vector<uint8_t> buffer(1024); //! @todo: fixed size should be removed
  uint8_t buffer_size = 4;
  buffer[0] = buffer[1] = buffer[2] = 0;
  buffer[3] = 1;

  //skip avc header
  size_t avc_data_size = avc.size();
  const uint8_t* avc_data = avc.data() + 6;
  avc_data_size -= 6;

  //sizeof SPS
  uint16_t sz = *avc_data;
  ++avc_data;
  --avc_data_size;
  sz = (sz << 8) | *avc_data;
  ++avc_data;
  --avc_data_size;
  //SPS
  memcpy(buffer.data() + buffer_size, avc_data, sz);
  buffer_size += sz, avc_data_size -= sz, avc_data += sz;

  // Number PPS
  sz = *avc_data, ++avc_data, --avc_data_size;

  while (sz--)
  {
    buffer[buffer_size] = buffer[buffer_size + 1] = buffer[buffer_size + 2] = 0;
    buffer[buffer_size + 3] = 1;
    buffer_size += 4;
    std::uint16_t ppssz(*avc_data);
    ++avc_data;
    --avc_data_size;
    ppssz = (ppssz << 8) | *avc_data;
    ++avc_data;
    --avc_data_size;
    std::memcpy(buffer.data() + buffer_size, avc_data, ppssz);
    buffer_size += ppssz;
    avc_data_size -= ppssz;
    avc_data += ppssz;
  }
  return {buffer.begin(), buffer.begin() + buffer_size};
}

void UTILS::ParseHeaderString(std::map<std::string, std::string>& headerMap,
                              const std::string& header)
{
  std::vector<std::string> headers = STRING::SplitToVec(header, '&');
  for (std::string& header : headers)
  {
    size_t pos = header.find('=');
    if (pos != std::string::npos)
    {
      std::string value = header.substr(pos + 1);
      headerMap[header.substr(0, pos)] = STRING::URLDecode(StringUtils::Trim(value));
    }
  }
}

uint64_t UTILS::GetTimestamp()
{
  auto currentTime = std::chrono::system_clock::now();
  auto epochTime = currentTime.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::seconds>(epochTime).count();
}

uint64_t UTILS::GetTimestampMs()
{
  auto currentTime = std::chrono::system_clock::now();
  auto epochTime = currentTime.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(epochTime).count();
}

std::vector<uint8_t> UTILS::ZeroPadding(const std::vector<uint8_t>& data, const size_t padSize)
{
  if (data.size() >= padSize || data.empty())
    return data;

  std::vector<uint8_t> paddedData(padSize, 0);
  std::copy(data.cbegin(), data.cend(), paddedData.begin() + (padSize - data.size()));
  return paddedData;
}

std::string UTILS::CODEC::FourCCToString(const uint32_t fourcc)
{
  std::string str;
  str += static_cast<char>((fourcc >> 24) & 255);
  str += static_cast<char>((fourcc >> 16) & 255);
  str += static_cast<char>((fourcc >> 8) & 255);
  str += static_cast<char>(fourcc & 255);
  return str;
}

bool UTILS::CODEC::Contains(const std::set<std::string>& list, std::string_view codec)
{
  return std::any_of(list.cbegin(), list.cend(),
                     [codec](const std::string& str) { return STRING::Contains(str, codec); });
}

bool UTILS::CODEC::Contains(const std::set<std::string>& list,
                            std::string_view codec,
                            std::string& codecStr)
{
  auto itCodec =
      std::find_if(list.cbegin(), list.cend(),
                   [codec](const std::string& str) { return STRING::Contains(str, codec); });
  if (itCodec != list.cend())
  {
    codecStr = *itCodec;
    return true;
  }
  codecStr.clear();
  return false;
}

std::string UTILS::CODEC::GetVideoDesc(const std::set<std::string>& list)
{
  for (const std::string& codec : list)
  {
    if (STRING::Contains(codec, FOURCC_AVC_) || STRING::Contains(codec, FOURCC_H264))
    {
      return "H.264";
    }
    if (STRING::Contains(codec, FOURCC_HEVC) || STRING::Contains(codec, FOURCC_HVC1) ||
        STRING::Contains(codec, FOURCC_DVH1) || STRING::Contains(codec, FOURCC_HEV1) ||
        STRING::Contains(codec, FOURCC_DVHE))
    {
      return "HEVC";
    }
    if (STRING::Contains(codec, FOURCC_VP09) || STRING::Contains(codec, NAME_VP9))
    {
      return "VP9";
    }
    if (STRING::Contains(codec, FOURCC_AV01) || STRING::Contains(codec, NAME_AV1))
    {
      return "AV1";
    }
  }
  return "";
}

bool UTILS::CODEC::IsAudio(std::string_view codec)
{
  for (const auto fourcc : CODEC::AUDIO_FOURCC_LIST)
  {
    if (STRING::Contains(codec, fourcc))
      return true;
  }
  for (const auto name : CODEC::AUDIO_NAME_LIST)
  {
    if (STRING::Contains(codec, name))
      return true;
  }
  return false;
}

bool UTILS::CODEC::IsVideo(std::string_view codec)
{
  for (const auto fourcc : CODEC::VIDEO_FOURCC_LIST)
  {
    if (STRING::Contains(codec, fourcc))
      return true;
  }
  for (const auto name : CODEC::VIDEO_NAME_LIST)
  {
    if (STRING::Contains(codec, name))
      return true;
  }
  return false;
}

bool UTILS::CODEC::IsSubtitleFourCC(std::string_view codec)
{
  for (const auto fourcc : CODEC::SUBTITLES_FOURCC_LIST)
  {
    if (STRING::Contains(codec, fourcc))
      return true;
  }
  return false;
}
