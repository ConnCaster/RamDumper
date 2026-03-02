// avml_min.cpp
// Minimal C++17 Linux port of AVML "dump.lime" logic (LiME v1 only).
// Sources: /dev/crash -> /proc/kcore -> /dev/mem
// Ranges: parsed from /proc/iomem (System RAM), merged.
// /proc/kcore: ELF64 PT_LOAD mapping to physical using first_vaddr - first_range_start logic.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

static constexpr size_t PAGE_SIZE = 0x1000;
static constexpr uint64_t MAX_BLOCK_SIZE = 0x1000ULL * 0x1000ULL; // 16MB
static constexpr uint32_t LIME_MAGIC = 0x4c694d45u; // "EMiL" as u32le
static constexpr uint32_t LIME_VERSION = 1;

struct Range64 {
    uint64_t start = 0;
    uint64_t end = 0; // exclusive
    bool empty() const { return end <= start; }
};

struct Block {
    uint64_t offset = 0;
    Range64 range;
    bool operator==(const Block& o) const {
        return offset == o.offset && range.start == o.range.start && range.end == o.range.end;
    }
};

static std::string sys_err(const char* what) {
    return std::string(what) + ": " + std::strerror(errno);
}

static bool can_open_ro(const char* path) {
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) { ::close(fd); return true; }
    return false;
}

static bool is_kcore_ok() {
    struct stat st{};
    if (::stat("/proc/kcore", &st) != 0) return false;
    if ((uint64_t)st.st_size <= 0x2000) return false;
    return can_open_ro("/proc/kcore");
}

// ------------------ /proc/iomem parsing & merging (Rust-equivalent) ------------------

static uint64_t parse_hex_u64(const std::string& s) {
    // Rust uses u64::from_str_radix(..., 16)
    char* endp = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(s.c_str(), &endp, 16);
    if (errno != 0 || endp == s.c_str() || *endp != '\0') {
        throw std::runtime_error("unable to parse hex: " + s);
    }
    return (uint64_t)v;
}

static std::vector<Range64> merge_ranges(std::vector<Range64> ranges) {
    std::vector<Range64> result;
    std::sort(ranges.begin(), ranges.end(),
              [](const Range64& a, const Range64& b){ return a.start < b.start; });

    while (!ranges.empty()) {
        Range64 cur = ranges.front();
        ranges.erase(ranges.begin());

        while (!ranges.empty() && cur.end >= ranges.front().start) {
            Range64 next = ranges.front();
            ranges.erase(ranges.begin());
            // Rust: range = range.start..next.end; (note: assumes sorted and overlapping/adjacent)
            cur.end = next.end;
        }
        result.push_back(cur);
    }
    return result;
}

static std::vector<Range64> parse_iomem_system_ram(const char* path = "/proc/iomem") {
    // Rust:
    // - skip lines starting with ' '
    // - keep lines ending with " : System RAM"
    // - parse "start-end"
    // - if start==0 && end==0 -> PermissionDenied
    FILE* f = std::fopen(path, "r");
    if (!f) throw std::runtime_error(sys_err("unable to read /proc/iomem"));

    std::vector<Range64> ranges;
    char* line = nullptr;
    size_t cap = 0;

    while (true) {
        ssize_t n = ::getline(&line, &cap, f);
        if (n < 0) break;

        std::string s(line);
        // trim trailing '\n'
        if (!s.empty() && s.back() == '\n') s.pop_back();

        if (!s.empty() && s[0] == ' ') continue;

        const std::string suffix = " : System RAM";
        if (s.size() < suffix.size() || s.compare(s.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;

        // take first token before space -> "start-end"
        auto sp = s.find(' ');
        std::string rangeTok = (sp == std::string::npos) ? s : s.substr(0, sp);

        auto dash = rangeTok.find('-');
        if (dash == std::string::npos) {
            throw std::runtime_error("unable to parse line: " + s);
        }

        std::string startHex = rangeTok.substr(0, dash);
        std::string endHex = rangeTok.substr(dash + 1);

        uint64_t start = parse_hex_u64(startHex);
        uint64_t end = parse_hex_u64(endHex);

        if (start == 0 && end == 0) {
            throw std::runtime_error("need CAP_SYS_ADMIN to read /proc/iomem (start==0,end==0)");
        }

        // Rust stores start..end (end is NOT +1 here; it uses the raw end field from iomem)
        ranges.push_back(Range64{start, end});
    }

    if (line) std::free(line);
    std::fclose(f);

    return merge_ranges(std::move(ranges));
}

// ------------------ LiME header (Rust-equivalent) ------------------

static void write_u32_le(int fd, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xff);
    b[1] = (uint8_t)((v >> 8) & 0xff);
    b[2] = (uint8_t)((v >> 16) & 0xff);
    b[3] = (uint8_t)((v >> 24) & 0xff);
    if (::write(fd, b, 4) != 4) throw std::runtime_error(sys_err("write_u32_le"));
}

static void write_u64_le(int fd, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)((v >> (8*i)) & 0xff);
    if (::write(fd, b, 8) != 8) throw std::runtime_error(sys_err("write_u64_le"));
}

static void write_lime_header(int dst_fd, const Range64& range) {
    // Rust Header::encode v1:
    // [magic, version] u32le + [start, end-1, padding=0] u64le
    // and range.end in header is EXCLUSIVE in struct, but stored as end-1 in file.
    write_u32_le(dst_fd, LIME_MAGIC);
    write_u32_le(dst_fd, LIME_VERSION);
    write_u64_le(dst_fd, range.start);
    uint64_t end_minus_1 = (range.end == 0) ? 0 : (range.end - 1);
    write_u64_le(dst_fd, end_minus_1);
    write_u64_le(dst_fd, 0);
}

// ------------------ Reader/Writer with page-aligned copy ------------------

class FDReader {
public:
    explicit FDReader(const std::string& path, bool align_pages)
        : path_(path), align_(align_pages) {
        fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd_ < 0) throw std::runtime_error(sys_err(("unable to open source " + path).c_str()));
    }
    ~FDReader() { if (fd_ >= 0) ::close(fd_); }
    int fd() const { return fd_; }
    bool align() const { return align_; }
    const std::string& path() const { return path_; }

    void seek_to(uint64_t off) {
        if (::lseek(fd_, (off_t)off, SEEK_SET) < 0) {
            throw std::runtime_error(sys_err("unable to seek source"));
        }
    }

    void read_exact(uint8_t* buf, size_t n) {
        size_t done = 0;
        while (done < n) {
            ssize_t r = ::read(fd_, buf + done, n - done);
            if (r < 0) throw std::runtime_error(sys_err("unable to read memory page"));
            if (r == 0) throw std::runtime_error("unexpected EOF while reading source");
            done += (size_t)r;
        }
    }

private:
    std::string path_;
    int fd_ = -1;
    bool align_ = false;
};

class FDWriter {
public:
    explicit FDWriter(const std::string& path) : path_(path) {
        // Rust unix: mode 0o600, create+truncate
        fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd_ < 0) throw std::runtime_error(sys_err(("unable to create dest " + path).c_str()));
    }
    ~FDWriter() { if (fd_ >= 0) ::close(fd_); }
    int fd() const { return fd_; }

    void write_all(const uint8_t* buf, size_t n) {
        size_t done = 0;
        while (done < n) {
            ssize_t w = ::write(fd_, buf + done, n - done);
            if (w < 0) throw std::runtime_error(sys_err("unable to write"));
            done += (size_t)w;
        }
    }

private:
    std::string path_;
    int fd_ = -1;
};

static void copy_bytes(size_t size, bool align_src, FDReader& src, FDWriter& dst) {
    if (align_src) {
        std::vector<uint8_t> buf(PAGE_SIZE);
        while (size >= PAGE_SIZE) {
            src.read_exact(buf.data(), PAGE_SIZE);
            dst.write_all(buf.data(), PAGE_SIZE);
            size -= PAGE_SIZE;
        }
        if (size > 0) {
            buf.assign(size, 0);
            src.read_exact(buf.data(), size);
            dst.write_all(buf.data(), size);
        }
    } else {
        // simple streaming copy
        std::vector<uint8_t> buf(1 << 20); // 1MB chunk
        size_t remaining = size;
        while (remaining > 0) {
            size_t want = std::min(remaining, buf.size());
            src.read_exact(buf.data(), want);
            dst.write_all(buf.data(), want);
            remaining -= want;
        }
    }
}

static inline uint64_t range_len(const Range64& r) { return (r.end >= r.start) ? (r.end - r.start) : 0; }

static void copy_block_impl_v1(FDReader& src, FDWriter& dst, const Range64& range, bool align_src) {
    // Rust: write_header(range) always
    write_lime_header(dst.fd(), range);

    uint64_t len64 = range_len(range);
    if (len64 > std::numeric_limits<size_t>::max())
        throw std::runtime_error("range too large for size_t");
    size_t size = (size_t)len64;

    // Rust behavior:
    // if size > MAX_BLOCK_SIZE => stream directly
    // else read whole block into memory, then if all zero -> return Ok() WITHOUT writing payload
    if (len64 > MAX_BLOCK_SIZE) {
        copy_bytes(size, align_src, src, dst);
        return;
    }

    // read into RAM (still page-by-page if align_src)
    std::vector<uint8_t> buf(size, 0);
    // emulate copy() into Cursor:
    // We'll fill buf by reading sequentially from src.
    if (align_src) {
        size_t done = 0;
        std::vector<uint8_t> page(PAGE_SIZE);
        while (done + PAGE_SIZE <= size) {
            src.read_exact(page.data(), PAGE_SIZE);
            std::memcpy(buf.data() + done, page.data(), PAGE_SIZE);
            done += PAGE_SIZE;
        }
        if (done < size) {
            size_t tail = size - done;
            std::vector<uint8_t> t(tail, 0);
            src.read_exact(t.data(), tail);
            std::memcpy(buf.data() + done, t.data(), tail);
        }
    } else {
        src.read_exact(buf.data(), size);
    }

    bool all_zero = true;
    for (uint8_t b : buf) { if (b != 0) { all_zero = false; break; } }
    if (all_zero) {
        // EXACT Rust behavior (see comment in assistant message)
        return;
    }

    dst.write_all(buf.data(), buf.size());
}

// ------------------ /proc/kcore minimal ELF64 PT_LOAD parser ------------------
// Enough to reproduce Rust mapping logic.

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

static void read_exact_fd(int fd, void* out, size_t n) {
    uint8_t* p = (uint8_t*)out;
    size_t done = 0;
    while (done < n) {
        ssize_t r = ::read(fd, p + done, n - done);
        if (r < 0) throw std::runtime_error(sys_err("read"));
        if (r == 0) throw std::runtime_error("unexpected EOF");
        done += (size_t)r;
    }
}

static void pread_exact_fd(int fd, void* out, size_t n, uint64_t off) {
    uint8_t* p = (uint8_t*)out;
    size_t done = 0;
    while (done < n) {
        ssize_t r = ::pread(fd, p + done, n - done, (off_t)(off + done));
        if (r < 0) throw std::runtime_error(sys_err("pread"));
        if (r == 0) throw std::runtime_error("unexpected EOF");
        done += (size_t)r;
    }
}

static std::vector<Block> find_kcore_blocks(const std::vector<Range64>& ranges,
                                            const std::vector<Block>& headers) {
    // Direct port of Snapshot::find_kcore_blocks
    std::vector<Block> result;

    for (const auto& r0 : ranges) {
        Range64 range = r0;

        for (const auto& header : headers) {
            auto contains = [&](const Range64& H, uint64_t v) -> bool {
                // Rust uses Range::contains which is [start, end)
                return v >= H.start && v < H.end;
            };

            bool c_start = contains(header.range, range.start);
            bool c_endm1 = contains(header.range, (range.end == 0 ? 0 : (range.end - 1)));

            if (c_start && c_endm1) {
                Block b;
                b.offset = header.offset + range.start - header.range.start; // saturating in Rust; here assume ok
                b.range = range;
                result.push_back(b);
                goto next_outer;
            } else if (c_start && !c_endm1) {
                Block b;
                b.offset = header.offset + range.start - header.range.start;
                b.range = Range64{range.start, header.range.end};
                result.push_back(b);
                range.start = header.range.end;
                // continue scanning next headers for remaining tail
            }
        }

    next_outer:
        (void)0;
    }

    return result;
}

static void dump_from_kcore(const std::vector<Range64>& mem_ranges, const std::string& dst_path) {
    if (!is_kcore_ok()) throw std::runtime_error("locked down /proc/kcore");

    // open src (/proc/kcore) and dst
    FDReader src("/proc/kcore", /*align_pages*/true);
    FDWriter dst(dst_path);

    // Read ELF header
    Elf64_Ehdr eh{};
    pread_exact_fd(src.fd(), &eh, sizeof(eh), 0);

    if (!(eh.e_ident[0] == ELFMAG0 && eh.e_ident[1] == ELFMAG1 &&
          eh.e_ident[2] == ELFMAG2 && eh.e_ident[3] == ELFMAG3))
        throw std::runtime_error("unable to parse elf: bad magic");
    if (eh.e_ident[4] != ELFCLASS64) throw std::runtime_error("unable to parse elf: not ELF64");
    if (eh.e_ident[5] != ELFDATA2LSB) throw std::runtime_error("unable to parse elf: not LSB");

    if (eh.e_phentsize != sizeof(Elf64_Phdr))
        throw std::runtime_error("unable to parse elf: unexpected phdr size");

    std::vector<Elf64_Phdr> phdrs(eh.e_phnum);
    pread_exact_fd(src.fd(), phdrs.data(), phdrs.size() * sizeof(Elf64_Phdr), eh.e_phoff);

    // Filter PT_LOAD, sort by p_vaddr
    std::vector<Elf64_Phdr> loads;
    loads.reserve(phdrs.size());
    for (const auto& p : phdrs) if (p.p_type == PT_LOAD) loads.push_back(p);
    std::sort(loads.begin(), loads.end(), [](const Elf64_Phdr& a, const Elf64_Phdr& b){
        return a.p_vaddr < b.p_vaddr;
    });

    if (loads.empty()) throw std::runtime_error("unable to create snapshot: no initial addresses");
    if (mem_ranges.empty()) throw std::runtime_error("unable to create snapshot: no initial memory range");

    uint64_t first_vaddr = loads.front().p_vaddr;
    uint64_t first_start = mem_ranges.front().start;
    uint64_t start = (first_vaddr >= first_start) ? (first_vaddr - first_start) : 0; // saturating_sub

    std::vector<Block> physical_ranges;
    physical_ranges.reserve(loads.size());

    for (const auto& ph : loads) {
        // entry_start = p_vaddr - start (checked_sub in Rust)
        if (ph.p_vaddr < start) throw std::runtime_error("unable to calculate start address");
        uint64_t entry_start = ph.p_vaddr - start;

        // entry_end = entry_start + p_memsz (checked_add)
        if (ph.p_memsz > std::numeric_limits<uint64_t>::max() - entry_start)
            throw std::runtime_error("unable to calculate end address");
        uint64_t entry_end = entry_start + ph.p_memsz;

        Block b;
        b.range = Range64{entry_start, entry_end};
        b.offset = ph.p_offset;
        physical_ranges.push_back(b);
    }

    std::vector<Block> blocks = find_kcore_blocks(mem_ranges, physical_ranges);

    // Write blocks: seek to offset, then copy range
    for (const auto& b : blocks) {
        if (b.offset > 0) src.seek_to(b.offset);
        copy_block_impl_v1(src, dst, b.range, src.align());
    }
}

static void dump_from_phys(const std::vector<Range64>& mem_ranges,
                           const std::string& src_path,
                           const std::string& dst_path) {
    bool is_crash = (src_path == "/dev/crash");
    bool align_src = (src_path == "/dev/crash" || src_path == "/dev/mem" || src_path == "/proc/kcore");

    FDReader src(src_path, align_src);
    FDWriter dst(dst_path);

    for (const auto& r : mem_ranges) {
        Block b;
        b.offset = r.start;
        if (is_crash) {
            // Rust: x.start..((x.end >> 12) << 12)
            uint64_t end_aligned = (r.end >> 12) << 12;
            b.range = Range64{r.start, end_aligned};
        } else {
            b.range = r;
        }

        if (b.offset > 0) src.seek_to(b.offset);
        copy_block_impl_v1(src, dst, b.range, src.align());
    }
}

// ------------------ Source selection (Rust-equivalent for "dump.lime") ------------------

static int run_dump(const std::string& dst_path) {
    std::vector<Range64> ranges = parse_iomem_system_ram();

    // Rust create():
    // for destination != stdout:
    // try /dev/crash, then /proc/kcore, then /dev/mem; collect reasons.
    std::string crash_err, kcore_err, mem_err;

    auto try_method = [&](auto&& fn, std::string& out_err) -> bool {
        try {
            fn();
            return true;
        } catch (const std::exception& e) {
            out_err = std::string("    ") + e.what();
            return false;
        }
    };

    if (try_method([&]{ dump_from_phys(ranges, "/dev/crash", dst_path); }, crash_err)) return 0;
    if (try_method([&]{ dump_from_kcore(ranges, dst_path); }, kcore_err)) return 0;
    if (try_method([&]{ dump_from_phys(ranges, "/dev/mem", dst_path); }, mem_err)) return 0;

    std::cerr << "error: unable to create memory snapshot\n";
    std::cerr << "    \n";
    std::cerr << crash_err << "\n";
    std::cerr << kcore_err << "\n";
    std::cerr << mem_err << "\n";
    return 2;
}

static void usage(const char* prog) {
    std::cerr << "Usage:\n"
              << "  sudo " << prog << " dump.lime\n"
              << "Notes:\n"
              << "  - LiME v1 only\n"
              << "  - Sources: /dev/crash -> /proc/kcore -> /dev/mem\n";
}

int main(int argc, char** argv) {
    // if (argc != 2) { usage(argv[0]); return 1; }
    std::string dst = "dump.lime"; // argv[1];
    if (geteuid() != 0) {
        std::cerr << "[WARNING] run as root for full memory access\n";
    }
    try {
        return run_dump(dst);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 3;
    }
}