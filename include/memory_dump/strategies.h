#ifndef STRATEGIES_H
#define STRATEGIES_H

#include <string>
#include <vector>

#include "memory_dump/interfaces/strategy_interface.h"
#include "memory_dump/types.h"

namespace memory_dump {

/**
 * @brief Стратегия дампа из устройств физической памяти.
 *
 * Поддерживает источники:
 * - `/dev/mem` — прямое отображение физической памяти
 * - `/dev/crash` — специальное устройство для crash-дампа (с выравниванием по страницам)
 * - `/proc/kcore` — ELF-образ ядра (с выравниванием по страницам)
 *
 * Особенности:
 * - Автоматическое определение необходимости выравнивания
 * - Специальная обработка /dev/crash (выравнивание адресов по границе страницы)
 * - Построчная обработка диапазонов памяти
 * - RAII через FileReader/FileWriter
 *
 * @note Для /dev/crash адреса выравниваются по PAGE_SIZE
 * @note Использует MemoryCopier для копирования блоков
 */
class PhysicalMemoryDumpStrategy : public IDumpStrategy {
public:
    explicit PhysicalMemoryDumpStrategy(std::string source_path);
    std::string Name() const override;

    /**
     * @brief Выполняет дамп памяти.
     *
     * Алгоритм:
     * 1. Проверяет доступность источника (CanOpenReadOnly)
     * 2. Определяет флаги выравнивания в зависимости от источника
     * 3. Открывает источник и приемник
     * 4. Для каждого диапазона памяти:
     *    - Пропускает пустые диапазоны
     *    - Для /dev/crash выравнивает адрес по PAGE_SIZE
     *    - Устанавливает позицию в источнике (SeekTo)
     *    - Копирует блок через MemoryCopier::CopyBlock
     *
     * @param memoryRanges Диапазоны физической памяти для копирования
     * @param destinationPath Путь к выходному файлу
     * @return 0 при успехе, 1 при ошибке
     *
     * @note Выбрасывает исключения через FileReader/FileWriter
     * @note При ошибках выводит сообщения в std::cerr
     */
    int Dump(const std::vector<Range64>& memoryRanges,
             const std::string& destinationPath) override;

private:
    std::string source_path_;
};

/**
 * @brief Специализированная стратегия для /proc/kcore.
 *
 * Отличается от PhysicalMemoryDumpStrategy использованием ELF-парсера
 * для корректного отображения виртуальных адресов из ELF-сегментов
 * на физические адреса.
 *
 * Особенности:
 * - Проверка корректности /proc/kcore через PosixUtil::IsKCoreOk()
 * - Использование KCoreElfParser для построения блоков
 * - Выравнивание по страницам включено всегда (align_pages = true)
 *
 * @note Требует валидного ELF-заголовка в /proc/kcore
 * @note Подходит для систем с поддержкой /proc/kcore
 */
class KCoreDumpStrategy : public IDumpStrategy {
public:
    std::string Name() const override;

    /**
    * @brief Выполняет дамп памяти из /proc/kcore.
    *
    * Алгоритм:
    * 1. Проверяет корректность /proc/kcore (размер > 8 КБ, доступность)
    * 2. Открывает /proc/kcore для чтения (с выравниванием)
    * 3. Открывает выходной файл для записи
    * 4. Создает парсер ELF-заголовков
    * 5. Строит блоки для копирования (BuildBlocks)
    * 6. Копирует каждый блок через MemoryCopier
    *
    * @param memoryRanges Диапазоны физической памяти (из /proc/iomem)
    * @param destinationPath Путь к выходному файлу
    * @return 0 при успехе, 1 при ошибке
    *
    * @note Использует KCoreElfParser для преобразования адресов
    * @note В отличие от PhysicalMemoryDumpStrategy, не использует SeekTo
    *       перед каждым блоком (смещения определяются парсером)
    */
    int Dump(const std::vector<Range64>& memoryRanges,
             const std::string& destinationPath) override;

private:
    static constexpr const char* kStrategyName = "/proc/kcore";
};

} // namespace avml

#endif //STRATEGIES_H
