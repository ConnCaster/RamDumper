#include "crash_strategy.h"

#include <fstream>
#include <sys/stat.h>

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
        return StrategyInfo{
            "/dev/crash",
            2,
            true,
            true,
            "Crash driver availability diagnostics"
        };
    }

    DumpResult CrashStrategy::dump(const std::string& output_path) {
        DumpResult result{false, "", 0, output_path};

        if (!isAvailable()) {
            result.error_message = "/dev/crash is not available (driver not loaded or inaccessible)";
            return result;
        }

        std::ofstream output(output_path.c_str(),
                             std::ios::out | std::ios::trunc);
        if (!output) {
            result.error_message = "Failed to create report file";
            return result;
        }

        output << "method=/dev/crash\n";
        output << "available=yes\n";
        output << "mode=diagnostic-only\n";
        output << "note=Live memory extraction is disabled in this build. "
                  "Use authorized crash dump workflows (e.g. kdump/vmcore) for offline analysis.\n";

        if (!output) {
            result.error_message = "Failed while writing report";
            return result;
        }

        result.success = true;
        result.bytes_dumped = 0;
        return result;
    }

} // namespace MemoryDump