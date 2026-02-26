#ifndef RAMDUMPER_DUMP_FACTORY_H
#define RAMDUMPER_DUMP_FACTORY_H

#include "dump_strategy.h"
#include <vector>

namespace MemoryDump {

    class DumpFactory {
    public:
        // Создает все доступные стратегии в порядке приоритета
        static std::vector<StrategyPtr> createStrategyChain();

        // Создает конкретную стратегию по имени
        static StrategyPtr createStrategy(const std::string& name);

        // Получает список всех зарегистрированных методов
        static std::vector<std::string> getAvailableMethods();
    };

} // namespace MemoryDump

#endif //RAMDUMPER_DUMP_FACTORY_H