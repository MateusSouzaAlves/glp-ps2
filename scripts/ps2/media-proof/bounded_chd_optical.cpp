/* Isolated derived Play! optical stream, pinned ABI/header unchanged.
 * SPDX-License-Identifier: MIT. Used by isolated derived PlayCore, not the product app. */
#include "discimages/ChdCdImageStream.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
uint64 big(const uint8* bytes, unsigned count) {
    uint64 value = 0;
    for(unsigned i = 0; i < count; ++i) value = (value << 8) | bytes[i];
    return value;
}
void exact(Framework::CStream& source, uint64 offset, void* out, uint64 length) {
    auto size = source.GetLength();
    if(offset < 124 || offset > size || length > size - offset)
        throw std::runtime_error("Optical metadata outside source");
    source.Seek(static_cast<int64>(offset), Framework::STREAM_SEEK_SET);
    if(source.Read(out, length) != length) throw std::runtime_error("Short optical metadata");
}
bool overlaps(uint64 begin, uint64 end, uint64 otherBegin, uint64 otherEnd) {
    return begin < otherEnd && otherBegin < end;
}
struct TrackText {
    unsigned number, frames;
    std::string_view type, gapType;
};
TrackText parseTrack(const std::vector<uint8>& bytes) {
    if(bytes.empty() || bytes.size() > 255 || bytes.back() != 0)
        throw std::runtime_error("Unbounded optical metadata string");
    for(size_t i = 0; i + 1 < bytes.size(); ++i)
        if(bytes[i] < 32 || bytes[i] > 126) throw std::runtime_error("Noncanonical optical metadata text");
    std::string_view remaining(reinterpret_cast<const char*>(bytes.data()), bytes.size() - 1);
    auto field = [&remaining](std::string_view label, bool last = false) {
        if(remaining.substr(0, label.size()) != label) throw std::runtime_error("Unexpected optical field");
        remaining.remove_prefix(label.size());
        auto end = last ? remaining.size() : remaining.find(' ');
        if(end == std::string_view::npos || !end || end > 31) throw std::runtime_error("Oversized optical token");
        auto value = remaining.substr(0, end);
        remaining.remove_prefix(end);
        if(!last) remaining.remove_prefix(1);
        return value;
    };
    auto decimal = [](std::string_view value, unsigned maxDigits, unsigned maximum) {
        if(value.size() > maxDigits) throw std::runtime_error("Oversized optical number");
        unsigned result = 0;
        for(char digit : value) {
            if(digit < '0' || digit > '9') throw std::runtime_error("Invalid optical number");
            auto next = static_cast<unsigned>(digit - '0');
            if(result > maximum / 10 || (result == maximum / 10 && next > maximum % 10))
                throw std::runtime_error("Optical number outside budget");
            result = result * 10 + next;
        }
        return result;
    };
    TrackText track{};
    track.number = decimal(field("TRACK:"), 2, 99);
    track.type = field("TYPE:");
    if(field("SUBTYPE:") != "NONE") throw std::runtime_error("Optical subchannel not enabled");
    track.frames = decimal(field("FRAMES:"), 7, 450000);
    if(field("PREGAP:") != "0") throw std::runtime_error("Optical pregap not enabled");
    track.gapType = field("PGTYPE:");
    if(field("PGSUB:") != "NONE" || field("POSTGAP:", true) != "0" || !remaining.empty())
        throw std::runtime_error("Optical gap not representable");
    return track; // Views remain valid until the caller's metadata buffer is reused.
}
}

CChdCdImageStream::CChdCdImageStream(std::unique_ptr<Framework::CStream> source)
    : CChdImageStream(std::move(source)) { ReadMetadata(); }
CChdCdImageStream::DATA_TYPE CChdCdImageStream::GetDataType() const { return m_dataType; }
const std::vector<CChdCdImageStream::TRACK>& CChdCdImageStream::GetTracks() const { return m_tracks; }

void CChdCdImageStream::ReadMetadata() {
    // Walk the original bounded chain, not chd_get_metadata's unbounded repeated search.
    uint8 header[124];
    m_baseStream->Seek(0, Framework::STREAM_SEEK_SET);
    if(m_baseStream->Read(header, sizeof(header)) != sizeof(header)) throw std::runtime_error("Short optical header");
    uint64 offset = big(header + 48, 8), map = big(header + 40, 8);
    uint64 mapEnd = map + ((GetTotalSize() + m_hunkSize - 1) / m_hunkSize) * 4;
    if(big(header + 16, 4)) {
        uint8 mapHeader[16]; exact(*m_baseStream, map, mapHeader, sizeof(mapHeader));
        mapEnd = map + 16 + big(mapHeader, 4);
    }
    if(m_unitSize == 2448 && GetTotalSize() > 2ULL * 1024 * 1024 * 1024)
        throw std::runtime_error("CD range exceeds current shared preflight budget");
    std::set<uint64> seen;
    std::vector<std::pair<uint64, uint64>> ranges;
    uint64 position = 0;
    bool dvd = false;
    while(offset) {
        if(!seen.insert(offset).second) throw std::runtime_error("Cyclic optical metadata");
        if(seen.size() > 128) throw std::runtime_error("Too many optical metadata records");
        uint8 record[16]; exact(*m_baseStream, offset, record, sizeof(record));
        auto flagsLength = big(record + 4, 4), length = flagsLength & 0xffffff;
        if(!length || length > 4096 || (flagsLength >> 24) > 1)
            throw std::runtime_error("Optical metadata length/flags outside budget");
        uint64 end = offset + 16 + length; // offset was range-checked against <=16 GiB.
        if(overlaps(offset, end, map, mapEnd)) throw std::runtime_error("Optical metadata overlaps map");
        for(auto range : ranges)
            if(overlaps(offset, end, range.first, range.second)) throw std::runtime_error("Overlapping optical records");
        ranges.emplace_back(offset, end);
        std::vector<uint8> bytes(static_cast<size_t>(length));
        exact(*m_baseStream, offset + 16, bytes.data(), length);
        std::string_view tag(reinterpret_cast<const char*>(record), 4);
        if(tag == "CHT2") {
            if(dvd || m_unitSize != 2448) throw std::runtime_error("Mixed CD/DVD optical geometry");
            auto track = parseTrack(bytes);
            if(track.number != m_tracks.size() + 1 || !track.frames)
                throw std::runtime_error("Invalid optical track sequence/length");
            DATA_TYPE type;
            if(track.type == "MODE1") type = DATA_TYPE_CD_MODE1;
            else if(track.type == "MODE1_RAW") type = DATA_TYPE_CD_MODE1_RAW;
            else if(track.type == "MODE2_RAW") type = DATA_TYPE_CD_MODE2_RAW;
            else if(track.type == "AUDIO") type = DATA_TYPE_CD_AUDIO;
            else throw std::runtime_error("Unsupported optical track type");
            if((track.number == 1) == (type == DATA_TYPE_CD_AUDIO))
                throw std::runtime_error("Expected first data track followed only by audio");
            if(track.number == 1) {
                if(track.gapType != "NONE" && track.gapType != "MODE1" && track.gapType != "MODE1_RAW"
                    && track.gapType != "MODE2_RAW" && track.gapType != "VMODE1"
                    && track.gapType != "VMODE1_RAW" && track.gapType != "VMODE2_RAW")
                    throw std::runtime_error("Unsupported first-track gap type");
                m_dataType = type;
            }
            auto padded = (static_cast<uint64>(track.frames) + 3) & ~3ULL;
            if(position > m_unitCount || padded > m_unitCount - position)
                throw std::runtime_error("Optical tracks exceed logical geometry");
            m_tracks.push_back({static_cast<uint32>(position), track.frames});
            if(type == DATA_TYPE_CD_AUDIO) {
                if(m_audioRangeStart == ~0ULL) m_audioRangeStart = position * static_cast<uint64>(m_unitSize);
                m_audioRangeEnd = (position + track.frames) * static_cast<uint64>(m_unitSize);
            }
            position += padded;
        } else if(tag == "DVD ") {
            if(dvd || !m_tracks.empty() || m_unitSize != 2048)
                throw std::runtime_error("Duplicate/mixed DVD metadata");
            dvd = true; m_dataType = DATA_TYPE_DVD;
        } else if(tag == "CHTR" || tag == "CHCD") {
            throw std::runtime_error("Legacy CD metadata not enabled");
        }
        offset = big(record + 8, 8);
    }
    if(!dvd && (m_tracks.empty() || position != m_unitCount))
        throw std::runtime_error("Optical metadata does not cover logical geometry");
}

uint64 CChdCdImageStream::Read(void* buffer, uint64 requested) {
    if(!requested) return 0;
    if(!buffer || requested > std::numeric_limits<size_t>::max()) throw std::invalid_argument("Invalid optical destination");
    if(m_dataType == DATA_TYPE_DVD || m_tracks.size() < 2) return CChdImageStream::Read(buffer, requested);
    auto out = static_cast<uint8*>(buffer);
    uint64 remaining = std::min(requested, GetTotalSize() - m_position), total = remaining;
    while(remaining) {
        // Keep data-only prefixes and trailing padding on the base hunk path, rather than
        // doing a track lookup for every sector of a large game data read.
        if(m_position < m_audioRangeStart || m_position >= m_audioRangeEnd) {
            auto chunk = m_position < m_audioRangeStart
                ? std::min<uint64>(remaining, m_audioRangeStart - m_position) : remaining;
            if(CChdImageStream::Read(out, chunk) != chunk) throw std::runtime_error("Short non-audio range read");
            out += chunk; remaining -= chunk;
            continue;
        }
        auto sector = m_position / m_unitSize, within = m_position % m_unitSize;
        bool audio = false;
        for(size_t i = 1; i < m_tracks.size(); ++i)
            if(sector >= m_tracks[i].startFrame && sector - m_tracks[i].startFrame < m_tracks[i].frames) { audio = true; break; }
        if(!audio || within >= 2352) {
            auto chunk = std::min<uint64>(remaining, m_unitSize - within);
            auto read = CChdImageStream::Read(out, chunk);
            if(read != chunk) throw std::runtime_error("Short optical sector read");
            out += chunk; remaining -= chunk;
            continue;
        }
        // Swap only valid 2352-byte CDDA payloads, never 96-byte subchannel or four-sector padding.
        // Pair-aligned bounded reads also handle single-byte/odd-position requests and short EOF.
        uint64 start = m_position, chunk = std::min<uint64>(remaining, 2352 - within);
        auto aligned = start & ~1ULL, alignedEnd = (start + chunk + 1) & ~1ULL;
        std::array<uint8, 2352> payload;
        CChdImageStream::Seek(static_cast<int64>(aligned), Framework::STREAM_SEEK_SET);
        try {
            if(CChdImageStream::Read(payload.data(), alignedEnd - aligned) != alignedEnd - aligned)
                throw std::runtime_error("Short CDDA payload read");
        } catch(...) {
            m_position = start; // A failed pair never enters the caller buffer or advances that segment.
            throw;
        }
        for(uint64 i = 0; i < chunk; ++i) out[i] = payload[(start - aligned + i) ^ 1ULL];
        m_position = start + chunk;
        out += chunk; remaining -= chunk;
    }
    return total;
}
