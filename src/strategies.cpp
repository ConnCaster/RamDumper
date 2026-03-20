#include "memory_dump/strategies.h"
#include "memory_dump/file_io.h"
#include "memory_dump/parsers.h"
#include "memory_dump/copier.h"

#include <iostream>

namespace memory_dump {

PhysicalMemoryDumpStrategy::PhysicalMemoryDumpStrategy(std::string source_path)
    : source_path_(std::move(source_path)) {}

std::string PhysicalMemoryDumpStrategy::Name() const {
    return source_path_;
}

int PhysicalMemoryDumpStrategy::Dump(const std::vector<Range64>& memoryRanges,
                                      const std::string& destinationPath) {
    if (!PosixUtil::CanOpenReadOnly(source_path_.c_str())) {
        std::cerr << "cannot open source: " << source_path_ << std::endl;
        return 1;
    }

    const bool isCrash = (source_path_ == "/dev/crash");
    const bool alignSrc = (source_path_ == "/dev/crash" ||
                           source_path_ == "/dev/mem" ||
                           source_path_ == "/proc/kcore");

    try {
        FileReader src(source_path_, alignSrc);
        FileWriter dst(destinationPath);

        for (const auto& r : memoryRanges) {
            if (r.Empty()) continue;

            Block block{r.start, isCrash ? Range64{r.start, (r.end >> 12) << 12} : r};
            if (isCrash && block.range.Empty()) continue;

            if (block.offset > 0 && src.SeekTo(block.offset) != 0) {
                std::cerr << "failed to seek to offset " << block.offset << std::endl;
                return 1;
            }
            if (MemoryCopier::CopyBlock(src, dst, block.range) != 0) {
                std::cerr << "failed to copy block [" << block.range.start 
                          << "-" << block.range.end << "]" << std::endl;
                return 1;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error in " << source_path_ << ": " << e.what() << std::endl;
        return 1;
    }
}

std::string KCoreDumpStrategy::Name() const {
    return kStrategyName;
}

int KCoreDumpStrategy::Dump(const std::vector<Range64>& memoryRanges,
                             const std::string& destinationPath) {
    if (!PosixUtil::IsKCoreOk()) {
        std::cerr << "locked down /proc/kcore" << std::endl;
        return 1;
    }

    try {
        FileReader src("/proc/kcore", true);
        FileWriter dst(destinationPath);

        KCoreElfParser parser(src);
        auto blocks = parser.BuildBlocks(memoryRanges);
        if (!blocks) {
            std::cerr << "failed to parse ELF file" << std::endl;
            return 1;
        }

        for (const auto& block : *blocks) {
            if (block.offset > 0 && src.SeekTo(block.offset) != 0) return 1;
            if (MemoryCopier::CopyBlock(src, dst, block.range) != 0) return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}

} // namespace avml