#ifndef RAMDUMPER_MEMORY_DUMPER_H
#define RAMDUMPER_MEMORY_DUMPER_H

#include "dump_strategy.h"

#include <functional>
#include <vector>

namespace MemoryDump {

    class MemoryDumper {
    public:
        typedef std::function<void(size_t current, size_t total)> ProgressCallback;

        MemoryDumper();
        ~MemoryDumper() = default;

        DumpResult dump(const std::string& output_path);
        DumpResult dumpWithMethod(const std::string& method_name,
                                  const std::string& output_path);
        std::vector<std::string> getAvailableMethods() const;
        void setProgressCallback(ProgressCallback callback);

    private:
        std::vector<StrategyPtr> strategies_;
        ProgressCallback progress_callback_;

        void log(const std::string& message) const;
        void logError(const std::string& message) const;
    };

} // namespace MemoryDump

#endif // RAMDUMPER_MEMORY_DUMPER_H