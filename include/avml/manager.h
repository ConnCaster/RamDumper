#ifndef MANAGER_H
#define MANAGER_H

#include <memory>
#include <string>
#include <vector>

#include "avml/interfaces/manager_interface.h"
#include "avml/types.h"
#include "avml/strategies.h"
#include "avml/parsers.h"

namespace avml {

/**
 * @brief Управляющий модуль для создания дампа памяти.
 *
 * Координирует процесс создания дампа:
 * 1. Парсит /proc/iomem для получения диапазонов "System RAM"
 * 2. Последовательно пробует стратегии дампа в порядке приоритета
 * 3. Возвращает результат операции
 *
 * Приоритет стратегий (от наивысшего к низшему):
 * 1. /dev/crash — специальное устройство для crash-дампа
 * 2. /proc/kcore — ELF-образ ядра (с ELF-парсингом)
 * 3. /dev/mem — прямое отображение физической памяти
 *
 * @note Стратегии перебираются до первой успешной
 * @note При неудаче всех стратегий возвращается ошибка
 */
class DumpModuleImpl : public IModuleImpl {
public:
    explicit DumpModuleImpl(const std::string& dump_file_path);
    ~DumpModuleImpl() = default;

    /**
     * @brief Запускает процесс создания дампа памяти.
     *
     * Алгоритм:
     * 1. Парсит /proc/iomem для получения диапазонов "System RAM"
     * 2. Если парсинг не удался → возвращает kError
     * 3. Перебирает все стратегии в порядке приоритета:
     *    - Вызывает Dump() для текущей стратегии
     *    - Если успешно (возврат 0) → возвращает kSuccess
     *    - Если ошибка → продолжает со следующей стратегией
     * 4. Если ни одна стратегия не успешна → возвращает kError
     *
     * @return ModuleResult
     *         - kSuccess — дамп успешно создан
     *         - kError — ошибка при парсинге iomem или все стратегии завершились ошибкой
     *
     * @note Результаты выполнения выводятся в std::cerr через стратегии и парсер
     */
    ModuleResult Run();

private:
    std::string dump_file_path_;
    std::vector<std::unique_ptr<IDumpStrategy>> strategies_;
    IoMemParser io_mem_parser_;
};

} // namespace avml

#endif //MANAGER_H
