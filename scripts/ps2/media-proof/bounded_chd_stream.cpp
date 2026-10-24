/* Isolated derived Play! stream, ABI/header unchanged. SPDX-License-Identifier: MIT
 * Shared positional discovery decoder; no Play! VM is loaded by the ARMSX2 product. */
#include "discimages/ChdImageStream.h"
#include "discimages/ChdStreamSupport.h"
#include <libchdr/chd.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include "compressed_map_guard.h"

namespace {
uint64 big(const uint8* bytes, unsigned count) {
    uint64 value = 0; for(unsigned i = 0; i < count; ++i) value = (value << 8) | bytes[i]; return value;
}
void preflight(Framework::CStream& stream) {
    constexpr uint64 budget = 16ULL * 1024 * 1024 * 1024;
    const auto size = stream.GetLength();
    if(size < 124 || size > budget) throw std::runtime_error("CHD source size outside budget");
    uint8 header[124]; stream.Seek(0, Framework::STREAM_SEEK_SET);
    if(stream.Read(header, sizeof(header)) != sizeof(header) || memcmp(header, "MComprHD", 8)
        || big(header + 8, 4) != 124 || big(header + 12, 4) != 5) throw std::runtime_error("Expected CHD v5");
    bool compressed = false, ended = false;
    std::set<uint64> codecs;
    for(unsigned i = 16; i < 32; i += 4) {
        auto codec = big(header + i, 4);
        if(!codec) { ended = true; continue; }
        if(ended || !codecs.insert(codec).second || (memcmp(header + i, "zlib", 4)
            && memcmp(header + i, "zstd", 4) && memcmp(header + i, "cdzl", 4) && memcmp(header + i, "cdzs", 4)
            && memcmp(header + i, "lzma", 4) && memcmp(header + i, "cdlz", 4)
            && memcmp(header + i, "flac", 4) && memcmp(header + i, "cdfl", 4) && memcmp(header + i, "huff", 4)))
            throw std::runtime_error("Compressed CHD codec not enabled");
        compressed = true;
    }
    for(unsigned i = 104; i < 124; ++i) if(header[i]) throw std::runtime_error("Parent CHD not enabled");
    auto logical = big(header + 32, 8), map = big(header + 40, 8), hunk = big(header + 56, 4), unit = big(header + 60, 4);
    if(!logical || logical > budget || !hunk || hunk > 512 * 1024 || (unit != 2048 && unit != 2448)
        || logical % unit || hunk % unit) throw std::runtime_error("Invalid CHD geometry");
    auto count = (logical + hunk - 1) / hunk;
    if(count > 1048576 || map < 124 || map >= size || (compressed ? 16ULL : count * 4) > size - map)
        throw std::runtime_error("Invalid CHD map budget/range");
    if(compressed) {
        if(unit != 2448) for(unsigned i = 16; i < 32; i += 4)
            if(header[i] == 'c' && header[i + 1] == 'd') throw std::runtime_error("CD codec with DVD units");
        uint8 mapHeader[16]; stream.Seek(static_cast<int64>(map), Framework::STREAM_SEEK_SET);
        if(stream.Read(mapHeader, sizeof(mapHeader)) != sizeof(mapHeader)) throw std::runtime_error("Short compressed map header");
        auto bytes = big(mapHeader, 4);
        if(!bytes || bytes > 16 * 1024 * 1024 || bytes > size - map - 16
            || mapHeader[12] > 24 || mapHeader[13] > 24 || mapHeader[14] || mapHeader[15])
            throw std::runtime_error("Compressed map length/bit budget");
    }
    stream.Seek(0, Framework::STREAM_SEEK_SET);
}
size_t safeRead(void* buffer, size_t unit, size_t count, core_file* file) noexcept {
    if(!unit || count > std::numeric_limits<size_t>::max() / unit) return 0;
    try {
        auto bytes = reinterpret_cast<Framework::CStream*>(file->argp)->Read(buffer, unit * count);
        return bytes <= unit * count ? static_cast<size_t>(bytes / unit) : 0;
    } catch(...) { return 0; } // Never unwind through the C decoder.
}
int safeSeek(core_file* file, int64_t position, int whence) noexcept {
    Framework::STREAM_SEEK_DIRECTION origin;
    switch(whence) { // Framework END/CUR values differ from stdio. Never cast.
        case SEEK_SET: origin = Framework::STREAM_SEEK_SET; break;
        case SEEK_CUR: origin = Framework::STREAM_SEEK_CUR; break;
        case SEEK_END: origin = Framework::STREAM_SEEK_END; break;
        default: return -1;
    }
    try { reinterpret_cast<Framework::CStream*>(file->argp)->Seek(position, origin); return 0; }
    catch(...) { return -1; }
}
uint64_t safeSize(core_file* file) noexcept {
    try { return reinterpret_cast<Framework::CStream*>(file->argp)->GetLength(); }
    catch(...) { return 0; }
}
int borrowedClose(core_file*) noexcept { return 0; }
}
core_file* ChdStreamSupport::CreateFileFromStream(Framework::CStream* stream) {
    if(!stream) throw std::invalid_argument("Null CHD stream");
    auto file = new core_file{}; file->argp = stream;
    file->fread = safeRead; file->fseek = safeSeek; file->fsize = safeSize; file->fclose = borrowedClose;
    return file; // Caller owns adapter; chd_close never deletes it or the stream.
}
CChdImageStream::CChdImageStream(std::unique_ptr<Framework::CStream> base) : m_baseStream(std::move(base)) {
    if(!m_baseStream) throw std::invalid_argument("Null original CHD");
    preflight(*m_baseStream);
    auto adapter = std::unique_ptr<core_file>(ChdStreamSupport::CreateFileFromStream(m_baseStream.get()));
    auto error = chd_open_core_file(adapter.get(), CHD_OPEN_READ, nullptr, &m_chd);
    if(error != CHDERR_NONE) throw std::runtime_error("Cannot open CHD: " + std::string(chd_error_string(error)));
    try {
        auto header = chd_get_header(m_chd);
        BoundedChd::validate(*header, *m_baseStream);
        m_unitCount = header->unitcount; m_unitSize = header->unitbytes; m_hunkSize = header->hunkbytes;
        m_hunkBuffer.resize(m_hunkSize);
    } catch(...) { chd_close(m_chd); m_chd = nullptr; throw; }
    m_file = adapter.release();
}
CChdImageStream::~CChdImageStream() { chd_close(m_chd); delete m_file; }
uint32 CChdImageStream::GetUnitSize() const { return m_unitSize; }
uint64 CChdImageStream::GetTotalSize() const { return m_unitCount * static_cast<uint64>(m_unitSize); }
uint64 CChdImageStream::Tell() { return m_position; }
bool CChdImageStream::IsEOF() { return m_position >= GetTotalSize(); }
void CChdImageStream::Seek(int64 displacement, Framework::STREAM_SEEK_DIRECTION origin) {
    uint64 base;
    switch(origin) {
        case Framework::STREAM_SEEK_SET: base = 0; break;
        case Framework::STREAM_SEEK_CUR: base = m_position; break;
        case Framework::STREAM_SEEK_END: base = GetTotalSize(); break;
        default: throw std::invalid_argument("Invalid CHD seek origin");
    }
    if(displacement < 0) {
        uint64 magnitude = static_cast<uint64>(-(displacement + 1)) + 1;
        if(magnitude > base) throw std::out_of_range("CHD seek before start");
        m_position = base - magnitude;
    } else {
        auto magnitude = static_cast<uint64>(displacement);
        if(base > GetTotalSize() || magnitude > GetTotalSize() - base) throw std::out_of_range("CHD seek beyond end");
        m_position = base + magnitude;
    }
}
uint64 CChdImageStream::Read(void* buffer, uint64 requested) {
    if(!requested) return 0;
    if(!buffer || requested > std::numeric_limits<size_t>::max()) throw std::invalid_argument("Invalid CHD destination");
    uint64 remaining = std::min(requested, GetTotalSize() - m_position), total = remaining;
    auto out = static_cast<uint8*>(buffer);
    while(remaining) {
        uint32 index = static_cast<uint32>(m_position / m_hunkSize), offset = static_cast<uint32>(m_position % m_hunkSize);
        if(index != m_hunkBufferIdx) {
            m_hunkBufferIdx = ~0U;
            auto error = chd_read(m_chd, index, m_hunkBuffer.data());
            if(error != CHDERR_NONE) throw std::runtime_error("Cannot decode CHD hunk: " + std::string(chd_error_string(error)));
            m_hunkBufferIdx = index; // Never publish a failed/stale buffer.
        }
        auto chunk = static_cast<size_t>(std::min<uint64>(remaining, m_hunkSize - offset));
        memcpy(out, m_hunkBuffer.data() + offset, chunk); out += chunk; m_position += chunk; remaining -= chunk;
    }
    return total;
}
uint64 CChdImageStream::Write(const void*, uint64) { throw std::runtime_error("Read-only CHD"); }
