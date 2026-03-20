#ifndef COPIER_H
#define COPIER_H

#include <vector>

#include "avml/types.h"
#include "avml/file_io.h"

namespace avml {

/**
 * @brief Класс для копирования блоков физической памяти.
 *
 * Выполняет копирование диапазонов памяти из источника (FileReader)
 * в приемник (FileWriter) с добавлением заголовков LiME.
 *
 * Стратегии копирования:
 * 1. **Прямое копирование** — для блоков ≤ MAX_BLOCK_SIZE
 *    - Выделяет буфер нужного размера
 *    - Читает все данные в буфер
 *    - Записывает буфер в выходной файл
 *
 * 2. **Потоковое копирование** — для блоков > MAX_BLOCK_SIZE
 *    - Копирует частями (страницами или чанками по 1 МБ)
 *    - Минимизирует использование памяти
 *
 * 3. **Выравнивание по страницам** — при align_pages = true
 *    - Читает постранично (PAGE_SIZE)
 *    - Обеспечивает выравнивание операций чтения
 *
 * @note Использует LimeFormatWriter для записи заголовков
 * @note MAX_BLOCK_SIZE определяется в types.h
 */
class MemoryCopier {
public:
    /**
     * @brief Копирует один блок памяти с заголовком.
     *
     * Алгоритм:
     * 1. Записывает заголовок LiME для диапазона
     * 2. Определяет размер блока
     * 3. Если блок > MAX_BLOCK_SIZE → потоковое копирование
     * 4. Иначе → прямое копирование с возможным выравниванием
     *
     * @param src Источник (FileReader для /proc/kcore)
     * @param dst Приемник (FileWriter для выходного файла)
     * @param range Диапазон физической памяти для копирования
     * @return 0 при успехе, 1 при ошибке
     *
     * @note При ошибке выводит сообщение в std::cerr
     */
    static int CopyBlock(FileReader& src, FileWriter& dst, const Range64& range);

private:
    /**
     * @brief Потоковое копирование больших блоков.
     *
     * Копирует данные частями, не выделяя буфер под весь блок.
     *
     * @param src Источник
     * @param dst Приемник
     * @param size Размер блока в байтах
     * @param alignSrc Флаг выравнивания чтения по страницам
     * @return 0 при успехе, 1 при ошибке
     *
     * Режимы:
     * - alignSrc = true: копирует страницами (PAGE_SIZE)
     * - alignSrc = false: копирует чанками по 1 МБ
     */
    static int CopyStreaming(FileReader& src, FileWriter& dst, size_t size, bool alignSrc);

    /**
     * @brief Заполняет буфер с выравниванием по страницам.
     *
     * Читает данные из источника в буфер, обеспечивая
     * чтение постранично для сохранения выравнивания.
     *
     * @param src Источник
     * @param buffer Целевой буфер
     * @return 0 при успехе, 1 при ошибке
     *
     * @note Используется когда align_pages = true
     */
    static int FillAligned(FileReader& src, std::vector<uint8_t>& buffer);
};

} // namespace avml

#endif //COPIER_H
