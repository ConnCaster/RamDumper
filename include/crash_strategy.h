#ifndef RAMDUMPER_CRASH_STRATEGY_H
#define RAMDUMPER_CRASH_STRATEGY_H

#include <cstdint>

#include "dump_strategy.h"

namespace MemoryDump {

    class CrashStrategy : public IDumpStrategy {
    public:
        CrashStrategy() = default;

        DumpResult dump(const std::string& output_path) override;
        bool isAvailable() const override;
        StrategyInfo getInfo() const override;
        std::string getName() const override { return "/dev/crash"; }

    private:
        struct MemoryRange {
            uint64_t start;
            uint64_t end;
            std::string name;
        };

        // Чтение физической памяти постранично через /dev/crash
        bool readPhysicalPages(const std::string& crash_path,
                               const std::string& output_path,
                               size_t& bytes_dumped);

        // Получение списка диапазонов RAM из /proc/iomem
        bool getRamRanges(std::vector<MemoryRange>& ranges);
    };

} // namespace MemoryDump

#endif //RAMDUMPER_CRASH_STRATEGY_H