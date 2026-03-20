#include "memory_dump/parsers.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>

namespace memory_dump {

std::optional<std::vector<Range64>> IoMemParser::ParseSystemRam() const {
    std::ifstream file(path_);
    if (!file.is_open()) {
        std::cerr << "Error opening file " << path_ << std::endl;
        return std::nullopt;
    }

    std::vector<Range64> ranges;
    std::string line;

    while (std::getline(file, line)) {
        if (!line.empty() && line[0] == ' ') continue;

        const std::string suffix = " : System RAM";
        if (line.size() < suffix.size() || 
            line.compare(line.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }

        auto sp = line.find(' ');
        std::string rangeToken = (sp == std::string::npos) ? line : line.substr(0, sp);
        auto dash = rangeToken.find('-');
        if (dash == std::string::npos) {
            std::cerr << "unable to parse line: " << line << std::endl;
            return std::nullopt;
        }

        auto start = ParseHexU64(rangeToken.substr(0, dash));
        auto end = ParseHexU64(rangeToken.substr(dash + 1));
        if (!start || !end || (*start == 0 && *end == 0)) {
            return std::nullopt;
        }
        ranges.emplace_back(Range64{*start, *end});
    }
    return MergeRanges(std::move(ranges));
}

std::optional<uint64_t> IoMemParser::ParseHexU64(const std::string& s) const {
    char* endp = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(s.c_str(), &endp, 16);
    if (errno != 0 || endp == s.c_str() || *endp != '\0') {
        std::cerr << "unable to parse hex string: " << s << std::endl;
        return std::nullopt;
    }
    return v;
}

std::vector<Range64> IoMemParser::MergeRanges(std::vector<Range64> ranges) const {
    std::vector<Range64> result;
    std::sort(ranges.begin(), ranges.end(), [](const Range64& a, const Range64& b) {
        return a.start < b.start;
    });
    for (const auto& r : ranges) {
        if (result.empty() || result.back().end < r.start) {
            result.push_back(r);
        } else {
            result.back().end = r.end;
        }
    }
    return result;
}

// ===== KCoreElfParser =====
// (сокращённо — реализация аналогична монолитной версии)
// ... [вставьте полную реализацию из монолитного файла] ...

std::optional<std::vector<Block>> KCoreElfParser::BuildBlocks(const std::vector<Range64>& memoryRanges) {
        Elf64_Ehdr eh{};
        int ret = src_.ReadFromPositionExact(&eh, sizeof(eh), 0);
        if (ret != 0) {
            std::cout << "Failed to read elf header: Elf64_Ehdr" << std::endl;
            return std::nullopt;
        }

        ret = ValidateElfHeader(eh);
        if (ret != 0) {
            std::cout << "Failed to validate elf header: Elf64_Ehdr" << std::endl;
            return std::nullopt;
        }

        if (eh.e_phentsize != sizeof(Elf64_Phdr)) {
            std::cout << "unable to parse elf: unexpected phdr size" << std::endl;
            return std::nullopt;
        }

        std::vector<Elf64_Phdr> phdrs(eh.e_phnum);
        ret = src_.ReadFromPositionExact(phdrs.data(),phdrs.size() * sizeof(Elf64_Phdr),eh.e_phoff);
        if (ret != 0) {
            std::cout << "Failed to read elf header: Elf64_Phdr" << std::endl;
            return std::nullopt;
        }

        std::vector<Elf64_Phdr> loads;
        for (const auto& ph : phdrs) {
            if (ph.p_type == PT_LOAD) {
                loads.push_back(ph);
            }
        }

        std::sort(
            loads.begin(),
            loads.end(),
            [](const Elf64_Phdr& a, const Elf64_Phdr& b) {
                return a.p_vaddr < b.p_vaddr;
            }
        );

        if (loads.empty()) {
            std::cout << "unable to create snapshot: no initial addresses" << std::endl;
            return std::nullopt;
        }
        if (memoryRanges.empty()) {
            std::cout << "unable to create snapshot: no initial memory range" << std::endl;
            return std::nullopt;
        }

        const uint64_t firstVaddr = loads.front().p_vaddr;
        const uint64_t firstStart = memoryRanges.front().start;
        const uint64_t start = (firstVaddr >= firstStart) ? (firstVaddr - firstStart) : 0;

        std::vector<Block> physicalRanges;
        physicalRanges.reserve(loads.size());

        for (const auto& ph : loads) {
            if (ph.p_vaddr < start) {
                std::cout << "unable to calculate start address" << std::endl;
                return std::nullopt;
            }

            const uint64_t entryStart = ph.p_vaddr - start;
            if (ph.p_memsz > std::numeric_limits<uint64_t>::max() - entryStart) {
                std::cout << "unable to calculate end address" << std::endl;
                return std::nullopt;
            }

            const uint64_t entryEnd = entryStart + ph.p_memsz;
            physicalRanges.push_back(Block{ph.p_offset, Range64{entryStart, entryEnd}});
        }
        return FindKcoreBlocks(memoryRanges, physicalRanges);
    }

int KCoreElfParser::ValidateElfHeader(const Elf64_Ehdr& eh) {
    if (!(eh.e_ident[0] == ELFMAG0 &&
        eh.e_ident[1] == ELFMAG1 &&
        eh.e_ident[2] == ELFMAG2 &&
        eh.e_ident[3] == ELFMAG3))
    {
        std::cout << "unable to parse elf: bad magic" << std::endl;
        return 1;
    }

    if (eh.e_ident[4] != ELFCLASS64) {
        std::cout << "unable to parse elf: not ELF64" << std::endl;
        return 1;
    }

    if (eh.e_ident[5] != ELFDATA2LSB) {
        std::cout << "unable to parse elf: not LSB" << std::endl;
        return 1;
    }
    return 0;
}

bool KCoreElfParser::Contains(const Range64& r, uint64_t value) {
    return value >= r.start && value < r.end;
}

std::vector<Block> KCoreElfParser::FindKcoreBlocks(const std::vector<Range64>& ranges,const std::vector<Block>& headers) {
    std::vector<Block> result;

    for (const auto& originalRange : ranges) {
        Range64 range = originalRange;

        bool finished = false;
        for (const auto& header : headers) {
            const bool containsStart = Contains(header.range, range.start);
            const bool containsEndMinus1 =
                Contains(header.range, (range.end == 0 ? 0 : range.end - 1));

            if (containsStart && containsEndMinus1) {
                result.push_back(Block{header.offset + range.start - header.range.start, range});
                finished = true;
                break;
            }

            if (containsStart && !containsEndMinus1) {
                result.push_back(Block{
                    header.offset + range.start - header.range.start,
                    Range64{range.start, header.range.end}
                });
                range.start = header.range.end;
            }
        }
    }
    return result;
}

int LimeFormatWriter::WriteHeader(FileWriter& dst, const Range64& range) {
    if (WriteU32Le(dst, LIME_MAGIC) != 0) return 1;
    if (WriteU32Le(dst, LIME_VERSION) != 0) return 1;
    if (WriteU64Le(dst, range.start) != 0) return 1;
    const uint64_t endMinus1 = (range.end == 0) ? 0 : (range.end - 1);
    if (WriteU64Le(dst, endMinus1) != 0) return 1;
    if (WriteU64Le(dst, 0) != 0) return 1;
    return 0;
}

int LimeFormatWriter::WriteU32Le(FileWriter& dst, uint32_t v) {
    uint8_t b[4] = {
        static_cast<uint8_t>(v & 0xff),
        static_cast<uint8_t>((v >> 8) & 0xff),
        static_cast<uint8_t>((v >> 16) & 0xff),
        static_cast<uint8_t>((v >> 24) & 0xff)
    };
    return dst.WriteAll(b, sizeof(b));
}

int LimeFormatWriter::WriteU64Le(FileWriter& dst, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xff);
    }
    return dst.WriteAll(b, sizeof(b));
}

} // namespace avml