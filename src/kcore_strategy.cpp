#include "kcore_strategy.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace MemoryDump {

bool KCoreStrategy::isAvailable() const {
    struct stat st;
    if (stat("/proc/kcore", &st) != 0) {
        return false;
    }

    const int fd = open("/proc/kcore", O_RDONLY);
    if (fd < 0) {
        return false;
    }

    close(fd);
    return true;
}

StrategyInfo KCoreStrategy::getInfo() const {
    return StrategyInfo{
        "/proc/kcore",
        1,
        true,
        false,
        "ELF metadata diagnostics for kernel core view"
    };
}

bool KCoreStrategy::parseElfSegments(const std::string& kcore_path,
                                     std::vector<Segment>& segments,
                                     std::string& error_message) const {
    const int fd = open(kcore_path.c_str(), O_RDONLY);
    if (fd < 0) {
        error_message = "Failed to open /proc/kcore";
        return false;
    }

    Elf64_Ehdr ehdr;
    const ssize_t ehdr_read = read(fd, &ehdr, sizeof(ehdr));
    if (ehdr_read != static_cast<ssize_t>(sizeof(ehdr))) {
        close(fd);
        error_message = "Failed to read ELF header";
        return false;
    }

    if (std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        close(fd);
        error_message = "Not an ELF file";
        return false;
    }

    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        close(fd);
        error_message = "Unsupported ELF class (expected ELF64)";
        return false;
    }

    if (ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
        close(fd);
        error_message = "Unexpected program header size";
        return false;
    }

    if (ehdr.e_phnum == 0) {
        close(fd);
        error_message = "No program headers found";
        return false;
    }

    if (lseek(fd, static_cast<off_t>(ehdr.e_phoff), SEEK_SET) < 0) {
        close(fd);
        error_message = "Failed to seek to program headers";
        return false;
    }

    std::vector<Elf64_Phdr> phdrs(static_cast<std::size_t>(ehdr.e_phnum));
    const std::size_t total_phdr_bytes = phdrs.size() * sizeof(Elf64_Phdr);

    const ssize_t phdr_read = read(fd, phdrs.data(), total_phdr_bytes);
    if (phdr_read != static_cast<ssize_t>(total_phdr_bytes)) {
        close(fd);
        error_message = "Failed to read program headers";
        return false;
    }

    for (std::size_t i = 0; i < phdrs.size(); ++i) {
        const Elf64_Phdr& phdr = phdrs[i];
        if (phdr.p_type != PT_LOAD) {
            continue;
        }

        Segment seg;
        seg.phys_addr = phdr.p_paddr;
        seg.virt_addr = phdr.p_vaddr;
        seg.mem_size = phdr.p_memsz;
        seg.file_size = phdr.p_filesz;
        seg.offset = phdr.p_offset;

        if (seg.mem_size == 0 && seg.file_size == 0) {
            continue;
        }

        segments.push_back(seg);

        if (progress_callback_) {
            progress_callback_(i + 1, phdrs.size());
        }
    }

    close(fd);

    if (segments.empty()) {
        error_message = "No PT_LOAD segments found";
        return false;
    }

    return true;
}

bool KCoreStrategy::writeReport(const std::string& output_path,
                                const std::vector<Segment>& segments,
                                std::size_t& bytes_described,
                                std::string& error_message) const {
    std::ofstream output(output_path.c_str(),
                         std::ios::out | std::ios::trunc);
    if (!output) {
        error_message = "Failed to create report file";
        return false;
    }

    bytes_described = 0;

    output << "method=/proc/kcore\n";
    output << "mode=diagnostic-only\n";
    output << "segment_count=" << segments.size() << "\n\n";

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const Segment& seg = segments[i];

        output << "[segment " << i << "]\n";
        output << "virt_addr=0x" << std::hex << seg.virt_addr << std::dec << '\n';
        output << "phys_addr=0x" << std::hex << seg.phys_addr << std::dec << '\n';
        output << "file_offset=" << seg.offset << '\n';
        output << "file_size=" << seg.file_size << '\n';
        output << "mem_size=" << seg.mem_size << "\n\n";

        bytes_described += static_cast<std::size_t>(seg.file_size);
    }

    if (!output) {
        error_message = "Failed while writing report";
        return false;
    }

    return true;
}

DumpResult KCoreStrategy::dump(const std::string& output_path) {
    DumpResult result{false, "", 0, output_path};

    if (!isAvailable()) {
        result.error_message = "/proc/kcore is not available or inaccessible";
        return result;
    }

    std::vector<Segment> segments;
    std::string error_message;

    if (!parseElfSegments("/proc/kcore", segments, error_message)) {
        result.error_message = error_message;
        return result;
    }

    std::cout << "[INFO] Found " << segments.size() << " PT_LOAD segments\n";

    if (!writeReport(output_path, segments, result.bytes_dumped, error_message)) {
        result.error_message = error_message;
        return result;
    }

    result.success = true;
    return result;
}

} // namespace MemoryDump