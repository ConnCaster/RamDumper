#ifndef DUMP_STRATEGY_INTERFACE_H
#define DUMP_STRATEGY_INTERFACE_H

#include <vector>
#include <string>
#include "memory_dump/types.h"


/**
 * @file dump_strategy_interface.h
 * @brief Интерфейс стратегии дампа памяти.
 *
 * Определяет абстрактный интерфейс для различных стратегий
 * создания дампа физической памяти. Реализует паттерн "Стратегия",
 * позволяя подменять алгоритмы копирования памяти независимо
 * от клиентского кода.
 *
 * Каждая конкретная стратегия определяет:
 * - Источник памяти (/dev/mem, /dev/crash, /proc/kcore)
 * - Способ доступа к памяти (прямой доступ, ELF-парсинг)
 * - Правила выравнивания и обработки адресов
 */
namespace memory_dump {

/**
 * @brief Интерфейс стратегии дампа памяти.
 *
 * Предоставляет контракт для всех стратегий копирования физической памяти.
 *
 * Реализации:
 * - PhysicalMemoryDumpStrategy — для /dev/mem, /dev/crash, /proc/kcore
 * - KCoreDumpStrategy — специализированная для /proc/kcore с ELF-парсингом
 *
 * @note Все стратегии должны быть потокобезопасны (если используются в многопоточной среде)
 * @note Стратегии не должны хранить состояние между вызовами Dump()
 */
class IDumpStrategy {
public:
    virtual ~IDumpStrategy() = default;
    virtual std::string Name() const = 0;

    /**
     * @brief Выполняет дамп памяти.
     *
     * Копирует указанные диапазоны физической памяти из источника
     * в выходной файл. Реализация должна:
     * - Проверить доступность источника
     * - Открыть источник и приемник
     * - Скопировать все диапазоны в формате LiME
     * - Корректно обработать ошибки
     *
     * @param memoryRanges Диапазоны физической памяти для копирования
     *                     (обычно из /proc/iomem, тип "System RAM")
     * @param destinationPath Путь к выходному файлу для сохранения дампа
     * @return int
     *         - 0 — дамп успешно создан
     *         - 1 — ошибка (источник недоступен, ошибка чтения/записи и т.д.)
     *
     * @note Реализации должны выводить диагностику в std::cerr
     * @note При ошибке возможно частичное копирование (не рекомендуется)
     */
    virtual int Dump(const std::vector<memory_dump::Range64>& memoryRanges,const std::string& destinationPath) = 0;
};

} // namespace avml

#endif //DUMP_STRATEGY_INTERFACE_H
