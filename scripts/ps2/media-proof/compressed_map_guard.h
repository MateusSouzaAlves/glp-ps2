/* Bounded decoded-map validation before any hunk/codec read. SPDX-License-Identifier: MIT */
#pragma once
#include "Stream.h"
#include <libchdr/chd.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <vector>

namespace BoundedChd {
inline uint64 big(const uint8* bytes, unsigned width) {
    uint64 result = 0; for(unsigned i = 0; i < width; ++i) result = (result << 8) | bytes[i]; return result;
}
inline bool overlaps(uint64 a, uint64 b, uint64 c, uint64 d) { return a < d && c < b; }
inline void exact(Framework::CStream& source, uint64 at, void* out, uint64 length) {
    auto size = source.GetLength();
    if(at < 124 || at > size || length > size - at) throw std::runtime_error("Compressed CHD range outside source");
    source.Seek(static_cast<int64>(at), Framework::STREAM_SEEK_SET);
    if(source.Read(out, length) != length) throw std::runtime_error("Short compressed CHD read");
}
inline void validate(const chd_header& header, Framework::CStream& source) {
    if(!header.compression[0]) return; // Existing NONE hunk errors remain checked at read time.
    uint8 mapHeader[16]; exact(source, header.mapoffset, mapHeader, sizeof(mapHeader));
    auto mapEnd = header.mapoffset + 16 + big(mapHeader, 4), size = source.GetLength();
    std::vector<std::pair<uint64, uint64>> metadata;
    std::set<uint64> seen;
    uint64 next = header.metaoffset;
    while(next) {
        if(!seen.insert(next).second || seen.size() > 128) throw std::runtime_error("Compressed metadata cycle/budget");
        uint8 record[16]; exact(source, next, record, sizeof(record));
        auto length = big(record + 4, 4) & 0xffffff, end = next + 16 + length;
        if(!length || length > 4096 || end > size || (record[4] > 1)) throw std::runtime_error("Compressed metadata length/flags");
        if(overlaps(next, end, header.mapoffset, mapEnd)) throw std::runtime_error("Compressed metadata overlaps map");
        for(auto range : metadata) if(overlaps(next, end, range.first, range.second)) throw std::runtime_error("Compressed metadata overlap");
        metadata.emplace_back(next, end); next = big(record + 8, 8);
    }
    // 0=unvisited, 1=visiting, 2..66=validated path depth+2. <=1 MiB independent of map budget.
    std::vector<uint8> depths(header.hunkcount, 0);
    for(uint32 hunk = 0; hunk < header.hunkcount; ++hunk) {
        const auto entry = header.rawmap + static_cast<size_t>(hunk) * 12;
        auto type = entry[0];
        auto length = big(entry + 1, 3), at = big(entry + 4, 6);
        if(type == 5) {
            if(length || at >= header.hunkcount) throw std::runtime_error("Compressed SELF reference outside map");
        } else {
            if(type > 4 || (type < 4 && !header.compression[type])) throw std::runtime_error("Undeclared/parent compressed map type");
            if(!length || length > header.hunkbytes || (type == 4 && length != header.hunkbytes)
                || at < 124 || at > size || length > size - at) throw std::runtime_error("Compressed hunk range/length");
            if(overlaps(at, at + length, header.mapoffset, mapEnd)) throw std::runtime_error("Compressed hunk overlaps map");
            for(auto range : metadata) if(overlaps(at, at + length, range.first, range.second)) throw std::runtime_error("Compressed hunk overlaps metadata");
            depths[hunk] = 2;
        }
    }
    for(uint32 hunk = 0; hunk < header.hunkcount; ++hunk) {
        std::array<uint32, 64> path;
        unsigned used = 0;
        uint32 current = hunk;
        while(depths[current] < 2) {
            if(depths[current] == 1) throw std::runtime_error("Cyclic compressed SELF references");
            if(used == path.size()) throw std::runtime_error("Compressed SELF depth budget exceeded");
            path[used++] = current; depths[current] = 1;
            current = static_cast<uint32>(big(header.rawmap + static_cast<size_t>(current) * 12 + 4, 6));
        }
        unsigned depth = depths[current] - 2;
        if(depth + used > 64) throw std::runtime_error("Compressed SELF depth budget exceeded");
        while(used) depths[path[--used]] = static_cast<uint8>(++depth + 2);
    }
}
}
