#ifndef RAMDUMPER_DUMP_STRATEGY_H
#define RAMDUMPER_DUMP_STRATEGY_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace MemoryDump {

    // Результат операции
    struct DumpResult {
        bool success;
        std::string error_message;
        std::size_t bytes_dumped;
        std::string output_path;
    };

    // Метаданные о методе
    struct StrategyInfo {
        std::string name;
        int priority; // Чем меньше, тем выше приоритет
        bool requires_root;
        bool requires_module;
        std::string description;
    };

    // Абстрактная стратегия
    class IDumpStrategy {
    public:
        virtual ~IDumpStrategy() {}

        virtual DumpResult dump(const std::string& output_path) = 0;
        virtual bool isAvailable() const = 0;
        virtual StrategyInfo getInfo() const = 0;
        virtual std::string getName() const = 0;
    };

    typedef std::unique_ptr<IDumpStrategy> StrategyPtr;

} // namespace MemoryDump

#endif // RAMDUMPER_DUMP_STRATEGY_H