#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace room {

/*!****************************************************************************************
 * \brief Read-only memory mapping of a whole file.
 *
 * Lets Cap'n Proto readers touch only the pages they need, so multi-GB .room files
 * open without copying the payload into RAM. Base address is page aligned.
 *****************************************************************************************/
class MappedFile {
public:
    explicit MappedFile(const std::string &path);
    ~MappedFile();

    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;

    const void *                                        data() const { return m_data; }
    std::uint64_t                                       size() const { return m_size; }

private:
    void *                                              m_data = nullptr;
    std::uint64_t                                       m_size = 0;
#ifdef _WIN32
    void *                                              m_file = nullptr;
    void *                                              m_mapping = nullptr;
#else
    int                                                 m_fd = -1;
#endif
};

} // namespace room
