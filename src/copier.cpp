#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#include "memory_dump/copier.h"
#include "memory_dump/parsers.h"

namespace memory_dump {

int MemoryCopier::CopyBlock(FileReader& src, FileWriter& dst, const Range64& range) {
    if (LimeFormatWriter::WriteHeader(dst, range) != 0) return 1;

    const uint64_t len64 = range.Length();
    if (len64 > std::numeric_limits<size_t>::max()) {
        std::cerr << "range too large for size_t" << std::endl;
        return 1;
    }

    const size_t size = static_cast<size_t>(len64);
    if (len64 > MAX_BLOCK_SIZE) {
        return CopyStreaming(src, dst, size, src.AlignPages());
    }

    std::vector<uint8_t> buffer(size, 0);
    if (src.AlignPages()) {
        if (FillAligned(src, buffer) != 0) return 1;
    } else if (!buffer.empty()) {
        if (src.ReadExact(buffer.data(), buffer.size()) != 0) return 1;
    }

    if (!buffer.empty()) {
        if (dst.WriteAll(buffer.data(), buffer.size()) != 0) return 1;
    }
    return 0;
}

int MemoryCopier::CopyStreaming(FileReader& src, FileWriter& dst, size_t size, bool alignSrc) {
    if (alignSrc) {
        std::vector<uint8_t> page(PAGE_SIZE);
        size_t remaining = size;
        while (remaining >= PAGE_SIZE) {
            if (src.ReadExact(page.data(), PAGE_SIZE) != 0) return 1;
            if (dst.WriteAll(page.data(), PAGE_SIZE) != 0) return 1;
            remaining -= PAGE_SIZE;
        }
        if (remaining > 0) {
            std::vector<uint8_t> tail(remaining, 0);
            if (src.ReadExact(tail.data(), remaining) != 0) return 1;
            if (dst.WriteAll(tail.data(), remaining) != 0) return 1;
        }
    } else {
        std::vector<uint8_t> chunk(1 << 20);
        size_t remaining = size;
        while (remaining > 0) {
            size_t want = std::min(remaining, chunk.size());
            if (src.ReadExact(chunk.data(), want) != 0) return 1;
            if (dst.WriteAll(chunk.data(), want) != 0) return 1;
            remaining -= want;
        }
    }
    return 0;
}

int MemoryCopier::FillAligned(FileReader& src, std::vector<uint8_t>& buffer) {
    if (buffer.empty()) return 0;
    size_t done = 0;
    std::vector<uint8_t> page(PAGE_SIZE);
    while (done + PAGE_SIZE <= buffer.size()) {
        if (src.ReadExact(page.data(), PAGE_SIZE) != 0) return 1;
        std::memcpy(buffer.data() + done, page.data(), PAGE_SIZE);
        done += PAGE_SIZE;
    }
    if (done < buffer.size()) {
        const size_t tail = buffer.size() - done;
        std::vector<uint8_t> tmp(tail, 0);
        if (src.ReadExact(tmp.data(), tail) != 0) return 1;
        std::memcpy(buffer.data() + done, tmp.data(), tail);
    }
    return 0;
}

} // namespace avml