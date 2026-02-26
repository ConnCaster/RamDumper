#include "kcore_strategy.h"
#include <fstream>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <elf.h>
#include <cstring>
#include <iostream>
#include <vector>

namespace MemoryDump {

bool KCoreStrategy::isAvailable() const {
    // Проверка Lockdown
    std::ifstream lockdown("/sys/kernel/security/lockdown");
    std::string content;
    if (lockdown) {
        std::getline(lockdown, content);
        if (content.find("[integrity]") != std::string::npos ||
            content.find("[confidentiality]") != std::string::npos) {
            std::cerr << "[WARNING] Kernel Lockdown is active - /proc/kcore may be restricted\n";
            // Не возвращаем false, пробуем anyway
            }
    }

    struct stat st;
    if (stat("/proc/kcore", &st) != 0) {
        return false;
    }

    // Проверка прав доступа
    int fd = open("/proc/kcore", O_RDONLY);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

StrategyInfo KCoreStrategy::getInfo() const {
    return {
        .name = "/proc/kcore",
        .priority = 1,
        .requires_root = true,
        .requires_module = false,
        .description = "ELF core dump of kernel memory (user-space)"
    };
}

// Парсинг ELF заголовков для получения сегментов памяти
bool KCoreStrategy::parseElfSegments(const std::string& kcore_path,
                                     std::vector<Segment>& segments) {
    int fd = open(kcore_path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    // Читаем ELF заголовок
    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        close(fd);
        return false;
    }

    // Проверка магического числа ELF
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        close(fd);
        return false;
    }

    // Читаем заголовки программ (Program Headers)
    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);

    if (lseek(fd, ehdr.e_phoff, SEEK_SET) < 0) {
        close(fd);
        return false;
    }

    if (read(fd, phdrs.data(), ehdr.e_phnum * sizeof(Elf64_Phdr))
        != static_cast<ssize_t>(ehdr.e_phnum * sizeof(Elf64_Phdr))) {
        close(fd);
        return false;
    }

    // Отбираем только сегменты PT_LOAD (загружаемые сегменты памяти)
    for (const auto& phdr : phdrs) {
        if (phdr.p_type == PT_LOAD) {
            Segment seg;
            seg.phys_addr = phdr.p_paddr;  // Физический адрес
            seg.virt_addr = phdr.p_vaddr;  // Виртуальный адрес
            seg.size = phdr.p_memsz;       // Размер сегмента
            seg.offset = phdr.p_offset;    // Смещение в файле kcore

            // Фильтруем сегменты: нам нужна только реальная RAM
            // Обычно сегменты с p_paddr > 0 и разумного размера - это RAM
            if (seg.size > 0 && seg.size < (1024ULL * 1024 * 1024 * 1024)) { // < 1TB
                segments.push_back(seg);
            }
        }
    }

    close(fd);
    return !segments.empty();
}

// Чтение и запись сегментов с прогрессом
bool KCoreStrategy::readAndWriteSegments(const std::string& kcore_path,
                                         const std::string& output_path,
                                         const std::vector<Segment>& segments,
                                         size_t& bytes_dumped) {
    int kcore_fd = open(kcore_path.c_str(), O_RDONLY);
    if (kcore_fd < 0) {
        return false;
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        close(kcore_fd);
        return false;
    }

    bytes_dumped = 0;
    constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1MB буфер
    std::vector<char> buffer(BUFFER_SIZE);

    size_t total_size = 0;
    for (const auto& seg : segments) {
        total_size += seg.size;
    }

    // Читаем каждый сегмент
    for (const auto& seg : segments) {
        if (lseek(kcore_fd, static_cast<off_t>(seg.offset), SEEK_SET) < 0) {
            close(kcore_fd);
            return false;
        }

        size_t remaining = seg.size;
        while (remaining > 0) {
            size_t to_read = std::min(remaining, BUFFER_SIZE);
            ssize_t read_bytes = read(kcore_fd, buffer.data(), to_read);

            if (read_bytes < 0) {
                // Ошибка чтения (может быть из-за Lockdown или защиты памяти)
                // Продолжаем чтение следующих сегментов
                break;
            }

            if (read_bytes == 0) {
                break; // Конец сегмента
            }

            output.write(buffer.data(), read_bytes);
            if (!output) {
                close(kcore_fd);
                return false;
            }

            bytes_dumped += static_cast<size_t>(read_bytes);
            remaining -= static_cast<size_t>(read_bytes);

            // Callback прогресса
            if (progress_callback_) {
                progress_callback_(bytes_dumped, total_size);
            }
        }
    }

    close(kcore_fd);
    return true;
}

DumpResult KCoreStrategy::dump(const std::string& output_path) {
    DumpResult result{false, "", 0, output_path};

    const std::string kcore_path = "/proc/kcore";

    if (!isAvailable()) {
        result.error_message = "/proc/kcore is not available or accessible";
        return result;
    }

    std::vector<Segment> segments;
    if (!parseElfSegments(kcore_path, segments)) {
        result.error_message = "Failed to parse ELF segments from /proc/kcore";
        return result;
    }

    std::cout << "[INFO] Found " << segments.size() << " memory segments\n";

    size_t total_ram = 0;
    for (const auto& seg : segments) {
        total_ram += seg.size;
    }
    std::cout << "[INFO] Total RAM to dump: " << (total_ram / 1024 / 1024) << " MB\n";

    if (!readAndWriteSegments(kcore_path, output_path, segments, result.bytes_dumped)) {
        result.error_message = "Failed to read/write memory segments";
        return result;
    }

    result.success = (result.bytes_dumped > 0);
    
    if (!result.success) {
        result.error_message = "No data read from /proc/kcore (possibly blocked by Lockdown)";
    }
    
    return result;
}

} // namespace MemoryDump