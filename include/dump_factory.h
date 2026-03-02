#ifndef RAMDUMPER_DUMP_FACTORY_H
#define RAMDUMPER_DUMP_FACTORY_H

#include "dump_strategy.h"

#include <vector>

namespace MemoryDump {

    class DumpFactory {
    public:
        static std::vector<StrategyPtr> createStrategyChain();
        static StrategyPtr createStrategy(const std::string& name);
        static std::vector<std::string> getAvailableMethods();
    };

} // namespace MemoryDump

#endif // RAMDUMPER_DUMP_FACTORY_H