// avml_refactored.cpp
// Refactored class-based C++17 Linux port of AVML "dump.lime" logic.
// Preserves original behavior as closely as possible.

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace avml
{
    static constexpr size_t PAGE_SIZE = 0x1000;
    static constexpr uint64_t MAX_BLOCK_SIZE = 0x1000ULL * 0x1000ULL; // 16MB
    static constexpr uint32_t LIME_MAGIC = 0x4c694d45u; // "EMiL" as u32le
    static constexpr uint32_t LIME_VERSION = 1;

    struct Range64
    {
        uint64_t start = 0;
        uint64_t end = 0; // exclusive

        bool empty() const
        {
            return end <= start;
        }

        uint64_t length() const
        {
            return (end >= start) ? (end - start) : 0;
        }
    };

    struct Block
    {
        uint64_t offset = 0;
        Range64 range;

        bool operator==(const Block& other) const
        {
            return offset == other.offset &&
                range.start == other.range.start &&
                range.end == other.range.end;
        }
    };

    class PosixUtil
    {
    public:
        static std::string sysError(const std::string& what)
        {
            return what + ": " + std::strerror(errno);
        }

        static bool canOpenReadOnly(const char* path)
        {
            int fd = ::open(path, O_RDONLY | O_CLOEXEC);
            if (fd >= 0)
            {
                ::close(fd);
                return true;
            }
            return false;
        }

        static bool isKcoreOk()
        {
            struct stat st{};
            if (::stat("/proc/kcore", &st) != 0)
            {
                return false;
            }
            if (static_cast<uint64_t>(st.st_size) <= 0x2000)
            {
                return false;
            }
            return canOpenReadOnly("/proc/kcore");
        }
    };

    class FileReader
    {
    public:
        FileReader(std::string path, bool alignPages)
            : path_(std::move(path)), alignPages_(alignPages)
        {
            fd_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd_ < 0)
            {
                throw std::runtime_error(
                    PosixUtil::sysError("unable to open source " + path_));
            }
        }

        ~FileReader()
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
            }
        }

        FileReader(const FileReader&) = delete;
        FileReader& operator=(const FileReader&) = delete;

        int fd() const
        {
            return fd_;
        }

        bool alignPages() const
        {
            return alignPages_;
        }

        const std::string& path() const
        {
            return path_;
        }

        void seekTo(uint64_t offset)
        {
            if (::lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0)
            {
                throw std::runtime_error(PosixUtil::sysError("unable to seek source"));
            }
        }

        void readExact(void* out, size_t size)
        {
            auto* p = static_cast<uint8_t*>(out);
            size_t done = 0;

            while (done < size)
            {
                ssize_t r = ::read(fd_, p + done, size - done);
                if (r < 0)
                {
                    throw std::runtime_error(
                        PosixUtil::sysError("unable to read memory page"));
                }
                if (r == 0)
                {
                    throw std::runtime_error("unexpected EOF while reading source");
                }
                done += static_cast<size_t>(r);
            }
        }

        void preadExact(void* out, size_t size, uint64_t offset)
        {
            auto* p = static_cast<uint8_t*>(out);
            size_t done = 0;

            while (done < size)
            {
                ssize_t r = ::pread(
                    fd_,
                    p + done,
                    size - done,
                    static_cast<off_t>(offset + done)
                );
                if (r < 0)
                {
                    throw std::runtime_error(PosixUtil::sysError("pread"));
                }
                if (r == 0)
                {
                    throw std::runtime_error("unexpected EOF");
                }
                done += static_cast<size_t>(r);
            }
        }

    private:
        std::string path_;
        int fd_ = -1;
        bool alignPages_ = false;
    };

    class FileWriter
    {
    public:
        explicit FileWriter(std::string path) : path_(std::move(path))
        {
            fd_ = ::open(
                path_.c_str(),
                O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                0600
            );
            if (fd_ < 0)
            {
                throw std::runtime_error(
                    PosixUtil::sysError("unable to create dest " + path_));
            }
        }

        ~FileWriter()
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
            }
        }

        FileWriter(const FileWriter&) = delete;
        FileWriter& operator=(const FileWriter&) = delete;

        int fd() const
        {
            return fd_;
        }

        void writeAll(const void* data, size_t size)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            size_t done = 0;

            while (done < size)
            {
                ssize_t w = ::write(fd_, p + done, size - done);
                if (w < 0)
                {
                    throw std::runtime_error(PosixUtil::sysError("unable to write"));
                }
                done += static_cast<size_t>(w);
            }
        }

    private:
        std::string path_;
        int fd_ = -1;
    };

    class IomemParser
    {
    public:
        std::vector<Range64> parseSystemRam(const char* path = "/proc/iomem") const
        {
            FILE* f = std::fopen(path, "r");
            if (!f)
            {
                throw std::runtime_error(
                    PosixUtil::sysError("unable to read /proc/iomem"));
            }

            std::vector<Range64> ranges;
            char* line = nullptr;
            size_t cap = 0;

            try
            {
                while (true)
                {
                    ssize_t n = ::getline(&line, &cap, f);
                    if (n < 0)
                    {
                        break;
                    }

                    std::string s(line);
                    if (!s.empty() && s.back() == '\n')
                    {
                        s.pop_back();
                    }

                    if (!s.empty() && s[0] == ' ')
                    {
                        continue;
                    }

                    const std::string suffix = " : System RAM";
                    if (s.size() < suffix.size() ||
                        s.compare(s.size() - suffix.size(), suffix.size(), suffix) != 0)
                    {
                        continue;
                    }

                    auto sp = s.find(' ');
                    std::string rangeToken =
                        (sp == std::string::npos) ? s : s.substr(0, sp);

                    auto dash = rangeToken.find('-');
                    if (dash == std::string::npos)
                    {
                        throw std::runtime_error("unable to parse line: " + s);
                    }

                    const std::string startHex = rangeToken.substr(0, dash);
                    const std::string endHex = rangeToken.substr(dash + 1);

                    uint64_t start = parseHexU64(startHex);
                    uint64_t end = parseHexU64(endHex);

                    if (start == 0 && end == 0)
                    {
                        throw std::runtime_error(
                            "need CAP_SYS_ADMIN to read /proc/iomem (start==0,end==0)");
                    }

                    ranges.push_back(Range64{start, end});
                }

                if (line)
                {
                    std::free(line);
                }
                std::fclose(f);

                return mergeRanges(std::move(ranges));
            }
            catch (...)
            {
                if (line)
                {
                    std::free(line);
                }
                std::fclose(f);
                throw;
            }
        }

    private:
        static uint64_t parseHexU64(const std::string& s)
        {
            char* endp = nullptr;
            errno = 0;
            unsigned long long v = std::strtoull(s.c_str(), &endp, 16);
            if (errno != 0 || endp == s.c_str() || *endp != '\0')
            {
                throw std::runtime_error("unable to parse hex: " + s);
            }
            return static_cast<uint64_t>(v);
        }

        static std::vector<Range64> mergeRanges(std::vector<Range64> ranges)
        {
            std::vector<Range64> result;

            std::sort(
                ranges.begin(),
                ranges.end(),
                [](const Range64& a, const Range64& b)
                {
                    return a.start < b.start;
                }
            );

            for (const auto& r : ranges)
            {
                if (result.empty())
                {
                    result.push_back(r);
                    continue;
                }

                auto& back = result.back();
                if (back.end >= r.start)
                {
                    back.end = r.end;
                }
                else
                {
                    result.push_back(r);
                }
            }

            return result;
        }
    };

    class LimeFormatWriter
    {
    public:
        static void writeHeader(FileWriter& dst, const Range64& range)
        {
            writeU32Le(dst, LIME_MAGIC);
            writeU32Le(dst, LIME_VERSION);
            writeU64Le(dst, range.start);

            const uint64_t endMinus1 = (range.end == 0) ? 0 : (range.end - 1);
            writeU64Le(dst, endMinus1);
            writeU64Le(dst, 0);
        }

    private:
        static void writeU32Le(FileWriter& dst, uint32_t v)
        {
            uint8_t b[4];
            b[0] = static_cast<uint8_t>(v & 0xff);
            b[1] = static_cast<uint8_t>((v >> 8) & 0xff);
            b[2] = static_cast<uint8_t>((v >> 16) & 0xff);
            b[3] = static_cast<uint8_t>((v >> 24) & 0xff);
            dst.writeAll(b, sizeof(b));
        }

        static void writeU64Le(FileWriter& dst, uint64_t v)
        {
            uint8_t b[8];
            for (int i = 0; i < 8; ++i)
            {
                b[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xff);
            }
            dst.writeAll(b, sizeof(b));
        }
    };

    class MemoryCopier
    {
    public:
        static void copyBlockV1(FileReader& src, FileWriter& dst, const Range64& range)
        {
            LimeFormatWriter::writeHeader(dst, range);

            const uint64_t len64 = range.length();
            if (len64 > std::numeric_limits<size_t>::max())
            {
                throw std::runtime_error("range too large for size_t");
            }

            const size_t size = static_cast<size_t>(len64);

            if (len64 > MAX_BLOCK_SIZE)
            {
                copyStreaming(src, dst, size, src.alignPages());
                return;
            }

            std::vector<uint8_t> buffer(size, 0);

            if (src.alignPages())
            {
                fillAligned(src, buffer);
            }
            else if (!buffer.empty())
            {
                src.readExact(buffer.data(), buffer.size());
            }

            bool allZero = true;
            for (uint8_t b : buffer)
            {
                if (b != 0)
                {
                    allZero = false;
                    break;
                }
            }

            // Preserve original behavior:
            // header is already written, but zero-block payload is omitted.
            if (allZero)
            {
                return;
            }

            if (!buffer.empty())
            {
                dst.writeAll(buffer.data(), buffer.size());
            }
        }

    private:
        static void copyStreaming(
            FileReader& src,
            FileWriter& dst,
            size_t size,
            bool alignSrc
        )
        {
            if (alignSrc)
            {
                std::vector<uint8_t> page(PAGE_SIZE);
                size_t remaining = size;

                while (remaining >= PAGE_SIZE)
                {
                    src.readExact(page.data(), PAGE_SIZE);
                    dst.writeAll(page.data(), PAGE_SIZE);
                    remaining -= PAGE_SIZE;
                }

                if (remaining > 0)
                {
                    std::vector<uint8_t> tail(remaining, 0);
                    src.readExact(tail.data(), remaining);
                    dst.writeAll(tail.data(), remaining);
                }
                return;
            }

            std::vector<uint8_t> chunk(1 << 20);
            size_t remaining = size;

            while (remaining > 0)
            {
                size_t want = std::min(remaining, chunk.size());
                src.readExact(chunk.data(), want);
                dst.writeAll(chunk.data(), want);
                remaining -= want;
            }
        }

        static void fillAligned(FileReader& src, std::vector<uint8_t>& buffer)
        {
            if (buffer.empty())
            {
                return;
            }

            size_t done = 0;
            std::vector<uint8_t> page(PAGE_SIZE);

            while (done + PAGE_SIZE <= buffer.size())
            {
                src.readExact(page.data(), PAGE_SIZE);
                std::memcpy(buffer.data() + done, page.data(), PAGE_SIZE);
                done += PAGE_SIZE;
            }

            if (done < buffer.size())
            {
                const size_t tail = buffer.size() - done;
                std::vector<uint8_t> tmp(tail, 0);
                src.readExact(tmp.data(), tail);
                std::memcpy(buffer.data() + done, tmp.data(), tail);
            }
        }
    };

    class KcoreElfParser
    {
    public:
        explicit KcoreElfParser(FileReader& src) : src_(src)
        {
        }

        std::vector<Block> buildBlocks(const std::vector<Range64>& memoryRanges)
        {
            Elf64_Ehdr eh{};
            src_.preadExact(&eh, sizeof(eh), 0);

            validateElfHeader(eh);

            if (eh.e_phentsize != sizeof(Elf64_Phdr))
            {
                throw std::runtime_error("unable to parse elf: unexpected phdr size");
            }

            std::vector<Elf64_Phdr> phdrs(eh.e_phnum);
            src_.preadExact(
                phdrs.data(),
                phdrs.size() * sizeof(Elf64_Phdr),
                eh.e_phoff
            );

            std::vector<Elf64_Phdr> loads;
            for (const auto& ph : phdrs)
            {
                if (ph.p_type == PT_LOAD)
                {
                    loads.push_back(ph);
                }
            }

            std::sort(
                loads.begin(),
                loads.end(),
                [](const Elf64_Phdr& a, const Elf64_Phdr& b)
                {
                    return a.p_vaddr < b.p_vaddr;
                }
            );

            if (loads.empty())
            {
                throw std::runtime_error("unable to create snapshot: no initial addresses");
            }
            if (memoryRanges.empty())
            {
                throw std::runtime_error("unable to create snapshot: no initial memory range");
            }

            const uint64_t firstVaddr = loads.front().p_vaddr;
            const uint64_t firstStart = memoryRanges.front().start;
            const uint64_t start =
                (firstVaddr >= firstStart) ? (firstVaddr - firstStart) : 0;

            std::vector<Block> physicalRanges;
            physicalRanges.reserve(loads.size());

            for (const auto& ph : loads)
            {
                if (ph.p_vaddr < start)
                {
                    throw std::runtime_error("unable to calculate start address");
                }

                const uint64_t entryStart = ph.p_vaddr - start;

                if (ph.p_memsz > std::numeric_limits<uint64_t>::max() - entryStart)
                {
                    throw std::runtime_error("unable to calculate end address");
                }

                const uint64_t entryEnd = entryStart + ph.p_memsz;

                physicalRanges.push_back(Block{
                    ph.p_offset,
                    Range64{entryStart, entryEnd}
                });
            }

            return findKcoreBlocks(memoryRanges, physicalRanges);
        }

    private:
        struct Elf64_Ehdr
        {
            unsigned char e_ident[16];
            uint16_t e_type;
            uint16_t e_machine;
            uint32_t e_version;
            uint64_t e_entry;
            uint64_t e_phoff;
            uint64_t e_shoff;
            uint32_t e_flags;
            uint16_t e_ehsize;
            uint16_t e_phentsize;
            uint16_t e_phnum;
            uint16_t e_shentsize;
            uint16_t e_shnum;
            uint16_t e_shstrndx;
        };

        struct Elf64_Phdr
        {
            uint32_t p_type;
            uint32_t p_flags;
            uint64_t p_offset;
            uint64_t p_vaddr;
            uint64_t p_paddr;
            uint64_t p_filesz;
            uint64_t p_memsz;
            uint64_t p_align;
        };

        static constexpr uint32_t PT_LOAD = 1;
        static constexpr unsigned char ELFMAG0 = 0x7f;
        static constexpr unsigned char ELFMAG1 = 'E';
        static constexpr unsigned char ELFMAG2 = 'L';
        static constexpr unsigned char ELFMAG3 = 'F';
        static constexpr unsigned char ELFCLASS64 = 2;
        static constexpr unsigned char ELFDATA2LSB = 1;

        FileReader& src_;

        static void validateElfHeader(const Elf64_Ehdr& eh)
        {
            if (!(eh.e_ident[0] == ELFMAG0 &&
                eh.e_ident[1] == ELFMAG1 &&
                eh.e_ident[2] == ELFMAG2 &&
                eh.e_ident[3] == ELFMAG3))
            {
                throw std::runtime_error("unable to parse elf: bad magic");
            }

            if (eh.e_ident[4] != ELFCLASS64)
            {
                throw std::runtime_error("unable to parse elf: not ELF64");
            }

            if (eh.e_ident[5] != ELFDATA2LSB)
            {
                throw std::runtime_error("unable to parse elf: not LSB");
            }
        }

        static bool contains(const Range64& r, uint64_t value)
        {
            return value >= r.start && value < r.end;
        }

        static std::vector<Block> findKcoreBlocks(
            const std::vector<Range64>& ranges,
            const std::vector<Block>& headers
        )
        {
            std::vector<Block> result;

            for (const auto& originalRange : ranges)
            {
                Range64 range = originalRange;

                bool finished = false;
                for (const auto& header : headers)
                {
                    const bool containsStart = contains(header.range, range.start);
                    const bool containsEndMinus1 =
                        contains(header.range, (range.end == 0 ? 0 : range.end - 1));

                    if (containsStart && containsEndMinus1)
                    {
                        result.push_back(Block{
                            header.offset + range.start - header.range.start,
                            range
                        });
                        finished = true;
                        break;
                    }

                    if (containsStart && !containsEndMinus1)
                    {
                        result.push_back(Block{
                            header.offset + range.start - header.range.start,
                            Range64{range.start, header.range.end}
                        });
                        range.start = header.range.end;
                    }
                }

                (void)finished;
            }

            return result;
        }
    };

    class IDumpStrategy
    {
    public:
        virtual ~IDumpStrategy() = default;

        virtual const char* name() const = 0;

        virtual void dump(
            const std::vector<Range64>& memoryRanges,
            const std::string& destinationPath
        ) = 0;
    };

    class PhysicalMemoryDumpStrategy : public IDumpStrategy
    {
    public:
        explicit PhysicalMemoryDumpStrategy(std::string sourcePath)
            : sourcePath_(std::move(sourcePath))
        {
        }

        const char* name() const override
        {
            return sourcePath_.c_str();
        }

        void dump(
            const std::vector<Range64>& memoryRanges,
            const std::string& destinationPath
        ) override
        {
            const bool isCrash = (sourcePath_ == "/dev/crash");
            const bool alignSrc =
            (sourcePath_ == "/dev/crash" ||
                sourcePath_ == "/dev/mem" ||
                sourcePath_ == "/proc/kcore");

            FileReader src(sourcePath_, alignSrc);
            FileWriter dst(destinationPath);

            for (const auto& r : memoryRanges)
            {
                Block block;
                block.offset = r.start;

                if (isCrash)
                {
                    const uint64_t endAligned = (r.end >> 12) << 12;
                    block.range = Range64{r.start, endAligned};
                }
                else
                {
                    block.range = r;
                }

                if (block.offset > 0)
                {
                    src.seekTo(block.offset);
                }

                MemoryCopier::copyBlockV1(src, dst, block.range);
            }
        }

    private:
        std::string sourcePath_;
    };

    class KcoreDumpStrategy : public IDumpStrategy
    {
    public:
        const char* name() const override
        {
            return "/proc/kcore";
        }

        void dump(
            const std::vector<Range64>& memoryRanges,
            const std::string& destinationPath
        ) override
        {
            if (!PosixUtil::isKcoreOk())
            {
                throw std::runtime_error("locked down /proc/kcore");
            }

            FileReader src("/proc/kcore", true);
            FileWriter dst(destinationPath);

            KcoreElfParser parser(src);
            const std::vector<Block> blocks = parser.buildBlocks(memoryRanges);

            for (const auto& block : blocks)
            {
                if (block.offset > 0)
                {
                    src.seekTo(block.offset);
                }
                MemoryCopier::copyBlockV1(src, dst, block.range);
            }
        }
    };

    class DumpManager
    {
    public:
        int run(const std::string& destinationPath)
        {
            const std::vector<Range64> ranges = iomemParser_.parseSystemRam();

            std::vector<std::unique_ptr<IDumpStrategy>> strategies;
            strategies.push_back(
                std::make_unique<PhysicalMemoryDumpStrategy>("/dev/crash"));
            strategies.push_back(std::make_unique<KcoreDumpStrategy>());
            strategies.push_back(
                std::make_unique<PhysicalMemoryDumpStrategy>("/dev/mem"));

            std::vector<std::string> errors;
            errors.reserve(strategies.size());

            for (auto& strategy : strategies)
            {
                try
                {
                    strategy->dump(ranges, destinationPath);
                    return 0;
                }
                catch (const std::exception& e)
                {
                    errors.push_back(std::string("    ") + e.what());
                }
            }

            std::cerr << "error: unable to create memory snapshot\n";
            std::cerr << "    \n";
            for (const auto& err : errors)
            {
                std::cerr << err << '\n';
            }

            return 2;
        }

    private:
        IomemParser iomemParser_;
    };

    static void usage(const char* prog)
    {
        std::cerr
            << "Usage:\n"
            << "  sudo " << prog << " dump.lime\n"
            << "Notes:\n"
            << "  - LiME v1 only\n"
            << "  - Sources: /dev/crash -> /proc/kcore -> /dev/mem\n";
    }
} // namespace avml

int main(int argc, char** argv)
{
    std::string dst = "dump.lime";
    if (argc == 2)
    {
        dst = argv[1];
    }

    if (::geteuid() != 0)
    {
        std::cerr << "[WARNING] run as root for full memory access\n";
    }

    try
    {
        avml::DumpManager manager;
        return manager.run(dst);
    }
    catch (const std::exception& e)
    {
        std::cerr << "fatal: " << e.what() << '\n';
        return 3;
    }
}
