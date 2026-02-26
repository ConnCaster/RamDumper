#ifndef RAMDUMPER_DUMP_STRATEGY_H
#define RAMDUMPER_DUMP_STRATEGY_H

#include <string>
#include <memory>
#include <vector>

namespace MemoryDump {

    // Результат операции дампа
    struct DumpResult {
        bool success;
        std::string error_message;
        size_t bytes_dumped;
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

    // Абстрактная стратегия (Strategy Pattern)
    class IDumpStrategy {
    public:
        virtual ~IDumpStrategy() = default;

        // Попытка создать дамп
        virtual DumpResult dump(const std::string& output_path) = 0;

        // Проверка доступности метода на текущей системе
        virtual bool isAvailable() const = 0;

        // Информация о стратегии
        virtual StrategyInfo getInfo() const = 0;

        // Имя метода для логирования
        virtual std::string getName() const = 0;
    };

    using StrategyPtr = std::unique_ptr<IDumpStrategy>;

} // namespace MemoryDump

#endif //RAMDUMPER_DUMP_STRATEGY_H