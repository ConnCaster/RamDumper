#include "crash_strategy.h"

#include <fstream>
#include <sys/stat.h>
#include <iostream>

namespace MemoryDump {

    bool CrashStrategy::isAvailable() const {
        struct stat st;
        if (stat("/dev/crash", &st) != 0) {
            return false;
        }

        std::ifstream test("/dev/crash", std::ios::binary);
        return test.good();
    }

    StrategyInfo CrashStrategy::getInfo() const {
        return {
            .name = "/dev/crash",
            .priority = 2,
            .requires_root = true,
            .requires_module = true,
            .description = "Physical memory access via crash driver"
        };
    }

    DumpResult CrashStrategy::dump(const std::string& output_path) {
        DumpResult result{false, "", 0, output_path};

        const std::string crash_path = "/dev/crash";

        if (!isAvailable()) {
            result.error_message = "/dev/crash is not available (driver not loaded?)";
            return result;
        }

        std::ifstream input(crash_path, std::ios::binary);
        if (!input) {
            result.error_message = "Failed to open /dev/crash";
            return result;
        }

        std::ofstream output(output_path, std::ios::binary);
        if (!output) {
            result.error_message = "Failed to create output file";
            return result;
        }

        // В реальной реализации: постраничное чтение с использованием ioctl
        // для получения информации о страницах

        constexpr size_t BUFFER_SIZE = 1024 * 1024;
        std::vector<char> buffer(BUFFER_SIZE);
        size_t total_read = 0;

        // Упрощенное чтение
        while (input) {
            input.read(buffer.data(), BUFFER_SIZE);
            size_t bytes_read = input.gcount();
            if (bytes_read > 0) {
                output.write(buffer.data(), bytes_read);
                total_read += bytes_read;
            }
        }

        result.success = (total_read > 0);
        result.bytes_dumped = total_read;

        return result;
    }

} // namespace MemoryDump