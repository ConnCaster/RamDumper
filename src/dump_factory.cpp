#include "dump_factory.h"
#include "crash_strategy.h"
#include "kcore_strategy.h"

#include <algorithm>
#include <memory>

namespace MemoryDump {

    std::vector<StrategyPtr> DumpFactory::createStrategyChain() {
        std::vector<StrategyPtr> chain;
        chain.push_back(StrategyPtr(new KCoreStrategy()));
        chain.push_back(StrategyPtr(new CrashStrategy()));

        std::sort(chain.begin(), chain.end(),
                  [](const StrategyPtr& a, const StrategyPtr& b) {
                      return a->getInfo().priority < b->getInfo().priority;
                  });

        return chain;
    }

    StrategyPtr DumpFactory::createStrategy(const std::string& name) {
        if (name == "/proc/kcore" || name == "kcore") {
            return StrategyPtr(new KCoreStrategy());
        }

        if (name == "/dev/crash" || name == "crash") {
            return StrategyPtr(new CrashStrategy());
        }

        return StrategyPtr();
    }

    std::vector<std::string> DumpFactory::getAvailableMethods() {
        std::vector<std::string> methods;
        methods.push_back("/proc/kcore");
        methods.push_back("/dev/crash");
        return methods;
    }

} // namespace MemoryDump