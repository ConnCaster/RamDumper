#ifndef FILE_IO_H
#define FILE_IO_H

#include <string>

#include "avml/types.h"

namespace avml {

/**
 * @brief Утилитарный класс для POSIX-операций ввода-вывода.
 *
 * Предоставляет статические методы для:
 * - Формирования сообщений об ошибках через std::error_code
 * - Проверки доступности файлов с использованием std::filesystem
 * - Валидации файла ядра /proc/kcore
 */
class PosixUtil {
public:
    /**
    * @brief Формирует строку с описанием системной ошибки.
    * @param what Контекстное описание операции, при которой произошла ошибка.
    * @return Строка формата: "what: strerror(errno)".
    */
    static std::string SysError(const std::string& what);

    /**
     * @brief Проверяет возможность открытия файла в режиме только для чтения.
     *
     * Выполняет две проверки:
     * 1. Файл должен быть обычным (regular file) через std::filesystem
     * 2. Файл должен успешно открываться с флагом O_CLOEXEC
     *
     * @param path Путь к файлу.
     * @return true если файл существует, является обычным и доступен для чтения.
     */
    static bool CanOpenReadOnly(const char* path);

    /** @brief Минимальный размер файла ядра (core dump) в байтах. */
    static constexpr uint64_t kMinKCoreSize = 8 * 1024;

    /**
     * @brief Проверяет корректность файла ядра /proc/kcore.
     *
     * Условия корректности:
     * - Файл /proc/kcore существует
     * - Размер файла > kMinKCoreSize
     * - Файл успешно открывается на чтение
     *
     * @return true если файл ядра доступен и имеет корректный размер.
     */
    static bool IsKCoreOk();
};

/**
 * @brief Класс для чтения данных из файла с поддержкой больших файлов.
 *
 * Особенности реализации:
 * - Открытие файла с флагом O_CLOEXEC (автозакрытие при exec)
 * - Использование lseek64/pread64 для поддержки файлов >2 ГБ
 * - Чтение с обработкой EINTR (прерывание по сигналу)
 * - RAII: автоматическое закрытие файла в деструкторе
 *
 * Режим выравнивания align_pages предназначен для оптимизации
 * операций чтения с выравниванием по границам страниц памяти.
 *
 * @note Конструктор выбрасывает std::runtime_error при ошибках открытия
 * @note Методы чтения возвращают 0 при успехе, 1 при ошибке
 */
class FileReader {
public:
    explicit FileReader(const std::string& path, bool align_pages);
    ~FileReader();

    // Запрет копирования, разрешение перемещения
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;
    FileReader(FileReader&&) noexcept = default;
    FileReader& operator=(FileReader&&) noexcept = default;

    int Fd() const { return fd_; }
    bool AlignPages() const { return align_pages_; }

    /**
     * @brief Устанавливает позицию чтения в файле.
     *
     * Использует lseek64 для поддержки смещений >2 ГБ.
     *
     * @param offset Смещение от начала файла в байтах.
     * @return 0 при успехе, 1 при ошибке.
     */
    int SeekTo(uint64_t offset);

    /**
     * @brief Считывает точное количество байт с текущей позиции.
     *
     * Циклически читает до полного заполнения буфера.
     * Корректно обрабатывает EINTR (повторяет чтение после сигнала).
     *
     * @param out Указатель на буфер для чтения.
     * @param size Количество байт для чтения.
     * @return 0 при успехе, 1 при ошибке или достижении EOF.
     */
    int ReadExact(void* out, size_t size) const;

    /**
      * @brief Считывает точное количество байт с указанной позиции.
      *
      * Использует pread64 для атомарного чтения с заданной позиции.
      * Файловый курсор не изменяется, что позволяет безопасно
      * выполнять чтение параллельно с SeekTo.
      *
      * @param out Указатель на буфер для чтения.
      * @param size Количество байт для чтения.
      * @param offset Смещение от начала файла в байтах.
      * @return 0 при успехе, 1 при ошибке или достижении EOF.
      */
    int ReadFromPositionExact(void* out, size_t size, uint64_t offset) const;

private:
    int fd_ = -1;
    bool align_pages_ = false;
};

/**
 * @brief Класс для записи данных в файл.
 *
 * Особенности реализации:
 * - Создание файла с правами доступа 0600 (только владелец)
 * - Перезапись существующего файла (O_TRUNC)
 * - Флаг O_CLOEXEC для безопасного fork/exec
 * - RAII: автоматическое закрытие файла
 *
 * @note Конструктор выбрасывает std::runtime_error при ошибках открытия
 * @note WriteAll не обрабатывает EINTR (может быть прерван сигналом)
 */
class FileWriter {
public:
    explicit FileWriter(std::string path);
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    int Fd() const { return fd_; }

    /**
     * @brief Записывает все данные в файл.
     *
     * Циклически записывает данные до полной записи буфера.
     *
     * @param data Указатель на буфер с данными.
     * @param size Размер данных в байтах.
     * @return 0 при успехе, 1 при ошибке.
     *
     * @note Не обрабатывает EINTR (в отличие от ReadExact)
     */
    int WriteAll(const void* data, size_t size);

private:
    std::string path_;
    int fd_ = -1;
};

} // namespace avml

#endif //FILE_IO_H
