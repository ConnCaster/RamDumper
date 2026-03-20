#ifndef PASRSERS_H
#define PASRSERS_H

#include <optional>
#include <string>
#include <vector>

#include "memory_dump/types.h"
#include "memory_dump/file_io.h"

namespace memory_dump {

/**
 * @brief Парсер файла /proc/iomem.
 *
 * Извлекает диапазоны физической памяти типа "System RAM" из /proc/iomem.
 * Поддерживает:
 * - Парсинг шестнадцатеричных адресов
 * - Объединение пересекающихся и смежных диапазонов
 *
 * @note Формат строки: "адрес-адрес : System RAM"
 * @example "00000000-00000000 : System RAM"
 */
class IoMemParser {
public:
    explicit IoMemParser(const std::string& path = "/proc/iomem") : path_(path) {}

    /**
    * @brief Парсит все диапазоны "System RAM" из файла.
    *
    * Алгоритм:
    * 1. Читает файл построчно
    * 2. Отфильтровывает строки, не содержащие " : System RAM"
    * 3. Извлекает диапазон адресов (формат: "start-end")
    * 4. Парсит шестнадцатеричные значения
    * 5. Объединяет пересекающиеся и смежные диапазоны
    *
    * @return std::optional<std::vector<Range64>>
    *         - Диапазоны памяти при успехе
    *         - std::nullopt при ошибке парсинга или открытия файла
    *
    * @note При ошибках выводит сообщения в std::cerr
    */
    std::optional<std::vector<Range64>> ParseSystemRam() const;

private:
    /**
     * @brief Парсит шестнадцатеричную строку в uint64_t.
     * @param s Строка с шестнадцатеричным числом (без префикса 0x)
     * @return std::optional<uint64_t> Значение или nullopt при ошибке
     */
    std::optional<uint64_t> ParseHexU64(const std::string& s) const;

    /**
     * @brief Объединяет пересекающиеся и смежные диапазоны.
     *
     * Сортирует диапазоны по start и выполняет слияние:
     * - Если диапазоны пересекаются или смежны (end >= next.start)
     * - Расширяет текущий диапазон до max(end, next.end)
     *
     * @param ranges Вектор несортированных диапазонов
     * @return std::vector<Range64> Вектор объединенных диапазонов
     */
    std::vector<Range64> MergeRanges(std::vector<Range64> ranges) const;

private:
    const std::string path_;
};

/**
 * @brief Парсер ELF-заголовков /proc/kcore.
 *
 * Анализирует ELF-структуру /proc/kcore для построения отображения
 * между виртуальными адресами (из ELF) и физическими адресами (из iomem).
 *
 * Алгоритм работы:
 * 1. Читает и валидирует ELF-заголовок (e_ident, ELFCLASS64, ELFDATA2LSB)
 * 2. Читает програмные заголовки (program headers)
 * 3. Отбирает сегменты типа PT_LOAD
 * 4. Вычисляет смещение между первым виртуальным и физическим адресом
 * 5. Строит блоки для чтения памяти
 *
 * @note Использует FileReader для чтения из /proc/kcore
 */
class KCoreElfParser {
public:
    explicit KCoreElfParser(FileReader& src) : src_(src) {}

    /**
     * @brief Строит блоки для дампа памяти.
     *
     * На основе диапазонов физической памяти (из IoMemParser)
     * и ELF-заголовков (из /proc/kcore) строит список блоков,
     * которые необходимо скопировать.
     *
     * Каждый блок содержит:
     * - offset: смещение в файле /proc/kcore
     * - range: физический диапазон памяти
     *
     * @param memoryRanges Физические диапазоны памяти (из /proc/iomem)
     * @return std::optional<std::vector<Block>>
     *         - Блоки для чтения при успехе
     *         - std::nullopt при ошибках парсинга ELF
     *
     * @note Выводит ошибки в std::cout
     */
    std::optional<std::vector<Block>> BuildBlocks(const std::vector<Range64>& memoryRanges);

private:
    // ELF структуры (локальные, не экспортируются)
    struct Elf64_Ehdr {
        unsigned char e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint64_t e_entry;
        uint64_t e_phoff;
        uint64_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    };
    struct Elf64_Phdr {
        uint32_t p_type;
        uint32_t p_flags;
        uint64_t p_offset;
        uint64_t p_vaddr;
        uint64_t p_paddr;
        uint64_t p_filesz;
        uint64_t p_memsz;
        uint64_t p_align;};

    static constexpr uint32_t PT_LOAD = 1;
    static constexpr unsigned char ELFMAG0 = 0x7f;
    static constexpr unsigned char ELFMAG1 = 'E';
    static constexpr unsigned char ELFMAG2 = 'L';
    static constexpr unsigned char ELFMAG3 = 'F';
    static constexpr unsigned char ELFCLASS64 = 2;
    static constexpr unsigned char ELFDATA2LSB = 1;

    static int ValidateElfHeader(const Elf64_Ehdr& eh);
    static bool Contains(const Range64& r, uint64_t value);

    /**
     * @brief Находит блоки в /proc/kcore для заданных диапазонов.
     *
     * Для каждого физического диапазона ищет соответствующий
     * ELF-сегмент и вычисляет смещение в файле.
     *
     * @param ranges Физические диапазоны памяти
     * @param headers Блоки из ELF-заголовков
     * @return std::vector<Block> Блоки для чтения
     */
    static std::vector<Block> FindKcoreBlocks(const std::vector<Range64>& ranges, const std::vector<Block>& headers);

private:
    FileReader& src_;
};

/**
 * @brief Запись заголовков формата LiME v1.
 *
 * Формирует заголовки для выходного файла в формате LiME (Linux Memory Extractor).
 *
 * Структура заголовка:
 * - magic (4 байта): "LIME"
 * - version (4 байта): версия формата
 * - start (8 байт): начальный физический адрес
 * - end (8 байт): конечный физический адрес (end - 1)
 * - flags (8 байт): зарезервировано (0)
 *
 * @note Все значения записываются в little-endian формате
 */
class LimeFormatWriter {
public:
    static int WriteHeader(FileWriter& dst, const Range64& range);

private:
    static int WriteU32Le(FileWriter& dst, uint32_t v);
    static int WriteU64Le(FileWriter& dst, uint64_t v);
};

} // namespace avml

#endif //PASRSERS_H
