#include "dump_factory.h"
#include "kcore_strategy.h"
#include "crash_strategy.h"

#include <algorithm>

namespace MemoryDump {

    std::vector<StrategyPtr> DumpFactory::createStrategyChain() {
        std::vector<StrategyPtr> chain;

        // Создаем стратегии в порядке приоритета
        chain.push_back(std::make_unique<KCoreStrategy>());
        chain.push_back(std::make_unique<CrashStrategy>());

        // Сортируем по приоритету
        std::sort(chain.begin(), chain.end(),
                  [](const StrategyPtr& a, const StrategyPtr& b) {
                      return a->getInfo().priority < b->getInfo().priority;
                  });

        return chain;
    }

    StrategyPtr DumpFactory::createStrategy(const std::string& name) {
        if (name == "/proc/kcore" || name == "kcore") {
            return std::make_unique<KCoreStrategy>();
        }
        if (name == "/dev/crash" || name == "crash") {
            return std::make_unique<CrashStrategy>();
        }
        return nullptr;
    }

    std::vector<std::string> DumpFactory::getAvailableMethods() {
        return {"/proc/kcore", "/dev/crash"};
    }

} // namespace MemoryDump