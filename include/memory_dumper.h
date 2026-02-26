#ifndef RAMDUMPER_MEMORY_DUMPER_H
#define RAMDUMPER_MEMORY_DUMPER_H

#include "dump_strategy.h"

#include <vector>
#include <functional>

namespace MemoryDump {

    class MemoryDumper {
    public:
        using ProgressCallback = std::function<void(size_t current, size_t total)>;

        MemoryDumper();
        ~MemoryDumper() = default;

        // Попытка создать дамп всеми доступными методами по порядку
        DumpResult dump(const std::string& output_path);

        // Попытка создать дамп конкретным методом
        DumpResult dumpWithMethod(const std::string& method_name,
                                  const std::string& output_path);

        // Получить список доступных методов на этой системе
        std::vector<std::string> getAvailableMethods() const;

        // Установить callback для прогресса
        void setProgressCallback(ProgressCallback callback);

    private:
        std::vector<StrategyPtr> strategies_;
        ProgressCallback progress_callback_;

        // Логирование
        void log(const std::string& message) const;
        void logError(const std::string& message) const;
    };

} // namespace MemoryDump

#endif //RAMDUMPER_MEMORY_DUMPER_H