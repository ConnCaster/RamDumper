#ifndef TYPES_H
#define TYPES_H

#include <cstdint>

namespace memory_dump {


/**
 * @brief Размер страницы памяти в байтах.
 *
 * Стандартный размер страницы для x86/x64 архитектур (4 КБ).
 * Используется для выравнивания при чтении из /proc/kcore и /dev/crash.
 */
static constexpr size_t PAGE_SIZE = 0x1000;

/**
 * @brief Максимальный размер блока памяти для копирования в оперативной памяти.
 *
 * Блоки памяти, превышающие это значение, обрабатываются потоковым методом
 * (chunked streaming) для минимизации потребления памяти.
 *
 * @value 16 MB (0x1000 * 0x1000 байт)
 */
static constexpr uint64_t MAX_BLOCK_SIZE = 0x1000ULL * 0x1000ULL;

/**
 * @brief Магическое число формата LiME.
 *
 * Сигнатура заголовка каждого диапазона памяти в файле дампа.
 * Значение 0x4c694d45 представляет строку "EMiL" в little-endian порядке.
 *
 * @see https://github.com/504ensicsLabs/LiME
 */
static constexpr uint32_t LIME_MAGIC = 0x4c694d45u;

/**
 * @brief Версия спецификации формата LiME.
 *
 * Текущая реализация поддерживает версию 1, которая включает:
 * - Заголовок диапазона (28 байт)
 * - Физические адреса начала и конца
 * - Little-endian порядок байтов
 */
static constexpr uint32_t LIME_VERSION = 1;

/**
 * @brief Представляет полуоткрытый диапазон 64-битных адресов [start; end).
 *
 * Эта структура используется для описания диапазонов физической памяти,
 * полученных из /proc/iomem, а также для указания областей для копирования
 * в выходной файл дампа.
 */
struct Range64 {
    uint64_t start = 0;
    uint64_t end = 0;

    bool Empty() const { return end <= start; }
    uint64_t Length() const { return (end >= start) ? (end - start) : 0; }
};

/**
 * @brief Представляет блок памяти для копирования с указанием смещения в источнике.
 *
 * Эта структура связывает диапазон физических адресов (Range64) с соответствующим
 * смещением в файле-источнике (например, /proc/kcore). Используется парсером
 * KCoreElfParser для преобразования виртуальных адресов ELF в физические смещения.
 */
struct Block {
    uint64_t offset = 0;
    Range64 range;

    bool operator==(const Block& other) const {
        return offset == other.offset &&
               range.start == other.range.start &&
               range.end == other.range.end;
    }
};

/**
 * @brief Коды возврата для методов модулей системы.
 *
 * Перечисление определяет стандартные результаты выполнения операций
 * в менеджере дампов. Позволяет единообразно обрабатывать
 * успешные и ошибочные завершения на верхнем уровне.
 */
enum class ModuleResult : int {
    kError = 0,
    kSuccess = 1,
    kNotFound = 2,
    kNotSupported = 3
};

} // namespace avml

#endif //TYPES_H
