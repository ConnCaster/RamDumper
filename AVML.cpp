#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

namespace memory_dump {
    static constexpr size_t PAGE_SIZE = 0x1000;
    static constexpr uint64_t MAX_BLOCK_SIZE = 0x1000ULL * 0x1000ULL; // 16MB
    static constexpr uint32_t LIME_MAGIC = 0x4c694d45u; // "EMiL" as u32le
    static constexpr uint32_t LIME_VERSION = 1;

    struct Range64 {
        uint64_t start = 0;
        uint64_t end = 0; // не включая конец диапазона [_;_)

        bool Empty() const {
            return end <= start;
        }

        uint64_t Length() const {
            return (end >= start) ? (end - start) : 0;
        }
    };

    struct Block {
        uint64_t offset = 0;
        Range64 range;

        bool operator==(const Block& other) const {
            return offset == other.offset &&
                range.start == other.range.start &&
                range.end == other.range.end;
        }
    };

    class PosixUtil {
    public:
        static std::string SysError(const std::string& what) {
            std::error_code err_code(errno, std::generic_category());
            return what + ": " + err_code.message();
        }

        static bool CanOpenReadOnly(const char* path) {
            std::error_code ec;
            auto perms = fs::status(path, ec);

            if (ec) {
                return false;
            }

            // Проверяем, что файл существует и читаем
            if (!fs::is_regular_file(perms)) {
                return false;
            }

            // Попытка открыть для проверки прав
            int fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd >= 0) {
                close(fd);
                return true;
            }
            return false;
        }


        static constexpr uint64_t kMinKCoreSize = 8 * 1024;
        /*
            /proc/kcore - ELF-файл с заголовками ядра
            Минимальный размер:
                - ELF-заголовок: 64 байта
                - Program headers: несколько × 56 байт
                - Хотя бы одна страница памяти: 4 КБ
                - Итого: ~8 КБ разумный минимум
            Если меньше -> файл повреждён или недоступен
         */
        static bool IsKCoreOk() {
            struct stat st{};
            if (stat("/proc/kcore", &st) != 0) {
                return false;
            }
            if (static_cast<uint64_t>(st.st_size) <= kMinKCoreSize) {
                return false;
            }
            return CanOpenReadOnly("/proc/kcore");
        }
    };

    class FileReader {
    public:
        explicit FileReader(const std::string& path, bool align_pages)
            : fd_(open(path.c_str(), O_RDONLY | O_CLOEXEC)),
              align_pages_(align_pages)
        {
            if (fd_ < 0) {
                throw std::runtime_error(PosixUtil::SysError("unable to open source " + path));
            }
        }

        ~FileReader() {
            if (fd_ >= 0) {
                close(fd_);
            }
        }

        FileReader(const FileReader&) = delete;
        FileReader& operator=(const FileReader&) = delete;
        FileReader(FileReader&&) noexcept = default;
        FileReader& operator=(FileReader&&) noexcept = default;

        int Fd() const { return fd_; }
        bool AlignPages() const { return align_pages_; }

        int SeekTo(uint64_t offset) {
            if (lseek64(fd_, static_cast<off64_t>(offset), SEEK_SET) < 0) {
                std::cout << PosixUtil::SysError("unable to seek source") << std::endl;
                return 1;
            }
            return 0;
        }

        int ReadExact(void* out, size_t size) const {
            auto* p = static_cast<uint8_t*>(out);
            size_t done = 0;

            while (done < size) {
                ssize_t r = read(fd_, p + done, size - done);
                if (r < 0) {
                    if (errno == EINTR) continue;
                    std::cout << "unable to read file" << std::endl;
                    return 1;
                }
                if (r == 0) {
                    std::cout << "unexpected EOF" << std::endl;
                    return 1;
                }
                done += static_cast<size_t>(r);
            }
            return 0;
        }

        int ReadFromPositionExact(void* out, size_t size, uint64_t offset) const {
            auto* p = static_cast<uint8_t*>(out);
            size_t done = 0;

            while (done < size) {
                ssize_t r = pread64(fd_, p + done, size - done, static_cast<off64_t>(offset + done));
                if (r < 0) {
                    if (errno == EINTR) continue;
                    std::cout << "unable to read file" << std::endl;
                    return 1;
                }
                if (r == 0) {
                    std::cout << "unexpected EOF" << std::endl;
                    return 1;
                }
                done += static_cast<size_t>(r);
            }
            return 0;
        }

    private:
        int fd_ = -1;
        bool align_pages_ = false;
    };

    class FileWriter {
    public:
        explicit FileWriter(std::string path)
            : path_(std::move(path))
        {
            fd_ = open(
                path_.c_str(),
                O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                0600
            );
            if (fd_ < 0) {
                throw std::runtime_error(PosixUtil::SysError("unable to create dest " + path_));
            }
        }

        ~FileWriter() {
            if (fd_ >= 0) {
                close(fd_);
            }
        }

        FileWriter(const FileWriter&) = delete;
        FileWriter& operator=(const FileWriter&) = delete;

        int Fd() const {
            return fd_;
        }

        int WriteAll(const void* data, size_t size) {
            const auto* p = static_cast<const uint8_t*>(data);
            size_t done = 0;

            while (done < size) {
                ssize_t w = write(fd_, p + done, size - done);
                if (w < 0) {
                    std::cout << PosixUtil::SysError("unable to write") << std::endl;
                    return 1;
                }
                done += static_cast<size_t>(w);
            }
            return 0;
        }

    private:
        std::string path_;
        int fd_ = -1;
    };

    class IoMemParser {
    public:
        std::optional<std::vector<Range64>> ParseSystemRam() const {
            std::ifstream file(path_);
            if (!file.is_open()) {
                std::cout << "Error opening file " << path_ << std::endl;
                return std::nullopt;
            }

            std::vector<Range64> ranges;
            std::string file_line;

            while (std::getline(file, file_line)) {
                const std::string& iomem_line{file_line};

                if (!iomem_line.empty() && iomem_line[0] == ' ') {
                    continue;
                }

                const std::string suffix = " : System RAM";
                if (iomem_line.size() < suffix.size() || iomem_line.compare(iomem_line.size() - suffix.size(), suffix.size(), suffix) != 0) {
                    continue;
                }

                auto sp = iomem_line.find(' ');
                std::string rangeToken = (sp == std::string::npos) ? iomem_line : iomem_line.substr(0, sp);

                auto dash = rangeToken.find('-');
                if (dash == std::string::npos) {
                    std::cout << "unable to parse line: " << iomem_line << std::endl;
                    return std::nullopt;
                }

                const std::string startHex = rangeToken.substr(0, dash);
                const std::string endHex = rangeToken.substr(dash + 1);

                std::optional<uint64_t> start = ParseHexU64(startHex);
                std::optional<uint64_t> end = ParseHexU64(endHex);
                if (!start.has_value() || !end.has_value()) {
                    return std::nullopt;
                }

                if (start == 0 && end == 0) {
                    std::cout << "need CAP_SYS_ADMIN to read /proc/iomem (start==0,end==0)" << std::endl;
                    return std::nullopt;
                }

                ranges.push_back(Range64{start.value(), end.value()});
            }

            return MergeRanges(std::move(ranges));
        }

    private:
        std::optional<uint64_t> ParseHexU64(const std::string& s) const {
            char* endp = nullptr;
            errno = 0;   // сброс предыдущих ошибок, чтобы отловить переполнение при конвертации из 16-ричной системы
            unsigned long long v = std::strtoull(s.c_str(), &endp, 16);
            if (errno != 0 || endp == s.c_str() || *endp != '\0') {
                std::cout << "unable to parse hex string: " << s << std::endl;
                return std::nullopt;
            }
            return v;
        }

        std::vector<Range64> MergeRanges(std::vector<Range64> ranges) const {
            std::vector<Range64> result;

            std::sort(
                ranges.begin(),
                ranges.end(),
                [](const Range64& a, const Range64& b) {
                    return a.start < b.start;
                }
            );

            for (const auto& range : ranges) {
                if (result.empty()) {
                    result.push_back(range);
                    continue;
                }

                auto& back = result.back();
                if (back.end >= range.start) {
                    back.end = range.end;
                } else {
                    result.push_back(range);
                }
            }
            return result;
        }

    private:
        const std::string path_{"/proc/iomem"};
    };

    class LimeFormatWriter {
    public:
        static int WriteHeader(FileWriter& dst, const Range64& range) {
            if (WriteU32Le(dst, LIME_MAGIC) != 0) return 1;
            if (WriteU32Le(dst, LIME_VERSION) != 0) return 1;
            if (WriteU64Le(dst, range.start) != 0) return 1;

            const uint64_t endMinus1 = (range.end == 0) ? 0 : (range.end - 1);
            if (WriteU64Le(dst, endMinus1) != 0) return 1;
            if (WriteU64Le(dst, 0) != 0) return 1;
            return 0;
        }

    private:
        static int WriteU32Le(FileWriter& dst, uint32_t v) {
            uint8_t b[4];
            b[0] = static_cast<uint8_t>(v & 0xff);
            b[1] = static_cast<uint8_t>((v >> 8) & 0xff);
            b[2] = static_cast<uint8_t>((v >> 16) & 0xff);
            b[3] = static_cast<uint8_t>((v >> 24) & 0xff);
            return dst.WriteAll(b, sizeof(b));
        }

        static int WriteU64Le(FileWriter& dst, uint64_t v) {
            uint8_t b[8];
            for (int i = 0; i < 8; ++i) {
                b[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xff);
            }
            return dst.WriteAll(b, sizeof(b));
        }
    };

    class MemoryCopier {
    public:
        static int CopyBlock(FileReader& src, FileWriter& dst, const Range64& range) {
            if (LimeFormatWriter::WriteHeader(dst, range) != 0) {
                return 1;
            }

            const uint64_t len64 = range.Length();
            if (len64 > std::numeric_limits<size_t>::max()) {
                std::cout << "range too large for size_t" << std::endl;
                return 1;
            }

            const size_t size = static_cast<size_t>(len64);

            if (len64 > MAX_BLOCK_SIZE) {
                return CopyStreaming(src, dst, size, src.AlignPages());
            }

            std::vector<uint8_t> buffer(size, 0);

            if (src.AlignPages()) {
                if (FillAligned(src, buffer) != 0) {
                    return 1;
                }
            }
            else if (!buffer.empty()) {
                if (src.ReadExact(buffer.data(), buffer.size()) != 0) {
                    return 1;
                }
            }

            if (!buffer.empty()) {
                if (dst.WriteAll(buffer.data(), buffer.size()) != 0) {
                    return 1;
                }
            }
            return 0;
        }

    private:
        static int CopyStreaming(
            FileReader& src,
            FileWriter& dst,
            size_t size,
            bool alignSrc
        )
        {
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

        static int FillAligned(FileReader& src, std::vector<uint8_t>& buffer) {
            if (buffer.empty()) {
                return 0;
            }

            size_t done = 0;
            std::vector<uint8_t> page(PAGE_SIZE);

            while (done + PAGE_SIZE <= buffer.size()) {
                if (src.ReadExact(page.data(), PAGE_SIZE) != 0) {
                    return 1;
                }
                std::memcpy(buffer.data() + done, page.data(), PAGE_SIZE);
                done += PAGE_SIZE;
            }

            if (done < buffer.size()) {
                const size_t tail = buffer.size() - done;
                std::vector<uint8_t> tmp(tail, 0);
                src.ReadExact(tmp.data(), tail);
                std::memcpy(buffer.data() + done, tmp.data(), tail);
            }
            return 0;
        }
    };

    class KCoreElfParser {
    public:
        explicit KCoreElfParser(FileReader& src)
            : src_(src)
        {}

        std::optional<std::vector<Block>> BuildBlocks(const std::vector<Range64>& memoryRanges) {
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

    private:
        struct Elf64_Ehdr {
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

        struct Elf64_Phdr {
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

        static int ValidateElfHeader(const Elf64_Ehdr& eh) {
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

        static bool Contains(const Range64& r, uint64_t value) {
            return value >= r.start && value < r.end;
        }

        static std::vector<Block> FindKcoreBlocks(const std::vector<Range64>& ranges,const std::vector<Block>& headers) {
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
    };

    class IDumpStrategy {
    public:
        virtual ~IDumpStrategy() = default;
        virtual std::string Name() const = 0;
        virtual int Dump(const std::vector<Range64>& memoryRanges,const std::string& destinationPath) = 0;
    };

    class PhysicalMemoryDumpStrategy : public IDumpStrategy {
public:
    explicit PhysicalMemoryDumpStrategy(const std::string& source_path)
        : source_path_(source_path)
    {}

    std::string Name() const override {
        return source_path_;
    }

    int Dump(const std::vector<Range64>& memoryRanges, const std::string& destinationPath) override {
        // Проверка доступности источника
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
                // Пропускаем пустые диапазоны
                if (r.Empty()) {
                    continue;
                }

                Block block;
                block.offset = r.start;

                // Для /dev/crash выравниваем конец по странице
                if (isCrash) {
                    const uint64_t endAligned = (r.end >> 12) << 12;
                    if (endAligned <= r.start) {
                        continue;  // Пропускаем слишком маленькие диапазоны
                    }
                    block.range = Range64{r.start, endAligned};
                } else {
                    block.range = r;
                }

                // Seek только если нужно (оптимизация)
                if (block.offset > 0) {
                    if (src.SeekTo(block.offset) != 0) {
                        std::cerr << "failed to seek to offset " << block.offset << std::endl;
                        return 1;
                    }
                }

                if (MemoryCopier::CopyBlock(src, dst, block.range) != 0) {
                    std::cerr << "failed to copy block ["
                              << block.range.start << "-" << block.range.end << "]"
                              << std::endl;
                    return 1;
                }
            }
            return 0;
        }
        catch (const std::exception& e) {
            std::cerr << "error in " << source_path_ << ": " << e.what() << std::endl;
            return 1;
        }
    }

private:
    std::string source_path_;
};

    class KCoreDumpStrategy : public IDumpStrategy {
    public:
        std::string Name() const override {
            return strategy_name_;
        }

        int Dump(const std::vector<Range64>& memoryRanges,const std::string& destinationPath) override {
            if (!PosixUtil::IsKCoreOk()) {
                std::cout << "locked down /proc/kcore" << std::endl;
                return 1;
            }

            try {
                FileReader src("/proc/kcore", true);
                FileWriter dst(destinationPath);

                KCoreElfParser parser(src);
                std::optional<std::vector<Block>> blocks = parser.BuildBlocks(memoryRanges);
                if (!blocks.has_value()) {
                    std::cout << "failed to parse ELF file" << std::endl;
                    return 1;
                }

                for (const auto& block : blocks.value()) {
                    if (block.offset > 0) {
                        int ret = src.SeekTo(block.offset);
                        if (ret != 0) {
                            return 1;
                        }
                    }
                    if (MemoryCopier::CopyBlock(src, dst, block.range) == 1) {
                        return 1;
                    }
                }
                return 0;
            } catch (std::exception& e) {
                std::cout << e.what() << std::endl;
                return 1;
            }
        }

    private:
        const std::string strategy_name_ = "/proc/kcore";

    };

    enum class ModuleResult : int {
        kError = 0,
        kSuccess = 1,
        kNotFound = 2,
        kNotSupported = 3
    };

    class IModuleImpl {
    public:
        virtual ~IModuleImpl() = default;
        virtual ModuleResult Run() = 0;
    };

    class DumpModuleImpl final: public IModuleImpl {
    public:
        explicit DumpModuleImpl(const std::string& dump_file_path)
            : dump_file_path_(dump_file_path) {
            strategies_.push_back(std::make_unique<PhysicalMemoryDumpStrategy>("/dev/crash"));
            strategies_.push_back(std::make_unique<KCoreDumpStrategy>());
            strategies_.push_back(std::make_unique<PhysicalMemoryDumpStrategy>("/dev/mem"));
        }
        ~DumpModuleImpl() override = default;

        ModuleResult Run() override {
            std::optional<std::vector<Range64>> ranges = io_mem_parser_.ParseSystemRam();
            if (!ranges.has_value()) {
                return ModuleResult::kError;
            }

            for (const auto& strategy : strategies_) {
                int ret = strategy->Dump(ranges.value(), dump_file_path_);
                if (ret == 0) {
                    return ModuleResult::kSuccess;
                }
            }
            return ModuleResult::kError;
        }

    private:
        std::string dump_file_path_;
        std::vector<std::unique_ptr<IDumpStrategy>> strategies_;

        IoMemParser io_mem_parser_;
    };
} // namespace avml

int main() {
    if (geteuid() != 0) {
        std::cerr << "[WARNING] run as root for full memory access\n";
    }

    try {
        const std::string dst = "dump.lime";
        memory_dump::DumpModuleImpl manager(dst);
        memory_dump::ModuleResult ret = manager.Run();
        return (ret != memory_dump::ModuleResult::kError) ? 0 : 1;
    }
    catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 3;
    }
}
