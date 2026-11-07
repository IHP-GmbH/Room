#include "mapped_file.h"

#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace room {

#ifdef _WIN32

namespace {

std::wstring widenUtf8(const std::string &path)
{
    if (path.empty()) {
        return {};
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), out.data(), len);
    return out;
}

} // namespace

MappedFile::MappedFile(const std::string &path)
{
    HANDLE file = CreateFileW(widenUtf8(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot open file for reading: " + path);
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
        CloseHandle(file);
        throw std::runtime_error("Cannot read file size: " + path);
    }
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        throw std::runtime_error("Cannot map file: " + path);
    }
    void *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        throw std::runtime_error("Cannot map file view: " + path);
    }
    m_file = file;
    m_mapping = mapping;
    m_data = view;
    m_size = static_cast<std::uint64_t>(size.QuadPart);
}

MappedFile::~MappedFile()
{
    if (m_data != nullptr) {
        UnmapViewOfFile(m_data);
    }
    if (m_mapping != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_mapping));
    }
    if (m_file != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_file));
    }
}

#else

MappedFile::MappedFile(const std::string &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("Cannot open file for reading: " + path);
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        throw std::runtime_error("Cannot read file size: " + path);
    }
    void *view = ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        throw std::runtime_error("Cannot map file: " + path);
    }
    ::madvise(view, static_cast<std::size_t>(st.st_size), MADV_RANDOM);
    m_fd = fd;
    m_data = view;
    m_size = static_cast<std::uint64_t>(st.st_size);
}

MappedFile::~MappedFile()
{
    if (m_data != nullptr) {
        ::munmap(m_data, static_cast<std::size_t>(m_size));
    }
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}

#endif

} // namespace room
