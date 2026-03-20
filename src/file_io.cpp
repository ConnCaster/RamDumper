#include "avml/file_io.h"

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace avml {

std::string PosixUtil::SysError(const std::string& what) {
    std::error_code err_code(errno, std::generic_category());
    return what + ": " + err_code.message();
}

bool PosixUtil::CanOpenReadOnly(const char* path) {
    std::error_code ec;
    auto perms = fs::status(path, ec);
    if (ec || !fs::is_regular_file(perms)) {
        return false;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
}

bool PosixUtil::IsKCoreOk() {
    struct stat st{};
    if (stat("/proc/kcore", &st) != 0) {
        return false;
    }
    if (static_cast<uint64_t>(st.st_size) <= kMinKCoreSize) {
        return false;
    }
    return CanOpenReadOnly("/proc/kcore");
}

FileReader::FileReader(const std::string& path, bool align_pages)
    : fd_(open(path.c_str(), O_RDONLY | O_CLOEXEC)),
      align_pages_(align_pages)
{
    if (fd_ < 0) {
        throw std::runtime_error(PosixUtil::SysError("unable to open source " + path));
    }
}

FileReader::~FileReader() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

int FileReader::SeekTo(uint64_t offset) {
    if (lseek64(fd_, static_cast<off64_t>(offset), SEEK_SET) < 0) {
        std::cerr << PosixUtil::SysError("unable to seek source") << std::endl;
        return 1;
    }
    return 0;
}

int FileReader::ReadExact(void* out, size_t size) const {
    auto* p = static_cast<uint8_t*>(out);
    size_t done = 0;
    while (done < size) {
        ssize_t r = read(fd_, p + done, size - done);
        if (r < 0) {
            if (errno == EINTR) continue;
            std::cerr << "unable to read file" << std::endl;
            return 1;
        }
        if (r == 0) {
            std::cerr << "unexpected EOF" << std::endl;
            return 1;
        }
        done += static_cast<size_t>(r);
    }
    return 0;
}

int FileReader::ReadFromPositionExact(void* out, size_t size, uint64_t offset) const {
    auto* p = static_cast<uint8_t*>(out);
    size_t done = 0;
    while (done < size) {
        ssize_t r = pread64(fd_, p + done, size - done, static_cast<off64_t>(offset + done));
        if (r < 0) {
            if (errno == EINTR) continue;
            std::cerr << "unable to read file" << std::endl;
            return 1;
        }
        if (r == 0) {
            std::cerr << "unexpected EOF" << std::endl;
            return 1;
        }
        done += static_cast<size_t>(r);
    }
    return 0;
}

FileWriter::FileWriter(std::string path) : path_(std::move(path)) {
    fd_ = open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd_ < 0) {
        throw std::runtime_error(PosixUtil::SysError("unable to create dest " + path_));
    }
}

FileWriter::~FileWriter() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

int FileWriter::WriteAll(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        ssize_t w = write(fd_, p + done, size - done);
        if (w < 0) {
            std::cerr << PosixUtil::SysError("unable to write") << std::endl;
            return 1;
        }
        done += static_cast<size_t>(w);
    }
    return 0;
}

} // namespace avml