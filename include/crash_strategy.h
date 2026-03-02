#ifndef RAMDUMPER_CRASH_STRATEGY_H
#define RAMDUMPER_CRASH_STRATEGY_H

#include "dump_strategy.h"

namespace MemoryDump {

    class CrashStrategy : public IDumpStrategy {
    public:
        CrashStrategy() = default;

        DumpResult dump(const std::string& output_path) override;
        bool isAvailable() const override;
        StrategyInfo getInfo() const override;
        std::string getName() const override { return "/dev/crash"; }
    };

} // namespace MemoryDump

#endif // RAMDUMPER_CRASH_STRATEGY_H