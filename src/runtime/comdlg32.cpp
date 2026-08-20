#include "tradutorlinux/runtime/comdlg32.hpp"

#include <cstring>
#include <string>

#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/util/unicode.hpp"

namespace tradutorlinux {

namespace {

struct GuestOpenFileNameA {
    std::uint32_t struct_size;
    void* owner;
    void* instance;
    const char* filter;
    char* custom_filter;
    std::uint32_t max_cust_filter;
    std::uint32_t filter_index;
    char* file;
    std::uint32_t max_file;
    char* file_title;
    std::uint32_t max_file_title;
    const char* initial_dir;
    const char* title;
    std::uint32_t flags;
};

struct GuestOpenFileNameW {
    std::uint32_t struct_size;
    void* owner;
    void* instance;
    const std::uint16_t* filter;
    std::uint16_t* custom_filter;
    std::uint32_t max_cust_filter;
    std::uint32_t filter_index;
    std::uint16_t* file;
    std::uint32_t max_file;
    std::uint16_t* file_title;
    std::uint32_t max_file_title;
    const std::uint16_t* initial_dir;
    const std::uint16_t* title;
    std::uint32_t flags;
};

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_COMDLG_MSABI int tl_GetOpenFileNameA(void* open_filename) noexcept {
    if (open_filename == nullptr || !mapped_range(open_filename, sizeof(GuestOpenFileNameA), true)) {
        return 0;
    }
    auto* ofn = static_cast<GuestOpenFileNameA*>(open_filename);
    if (ofn->file != nullptr && ofn->max_file > 0 && mapped_range(ofn->file, ofn->max_file, true)) {
        if (ofn->file[0] == '\0') {
            std::strncpy(ofn->file, "C:\\document.txt", ofn->max_file - 1);
            ofn->file[ofn->max_file - 1] = '\0';
        }
        return 1;
    }
    return 0;
}

TL_COMDLG_MSABI int tl_GetOpenFileNameW(void* open_filename) noexcept {
    if (open_filename == nullptr || !mapped_range(open_filename, sizeof(GuestOpenFileNameW), true)) {
        return 0;
    }
    auto* ofn = static_cast<GuestOpenFileNameW*>(open_filename);
    if (ofn->file != nullptr && ofn->max_file > 0 && mapped_range(ofn->file, ofn->max_file * sizeof(std::uint16_t), true)) {
        if (ofn->file[0] == 0) {
            const std::u16string u16 = util::utf8_to_wide("C:\\document.txt");
            const std::size_t len = std::min<std::size_t>(u16.size(), ofn->max_file - 1);
            std::copy(u16.begin(), u16.begin() + static_cast<std::ptrdiff_t>(len), ofn->file);
            ofn->file[len] = 0;
        }
        return 1;
    }
    return 0;
}

TL_COMDLG_MSABI int tl_GetSaveFileNameA(void* open_filename) noexcept {
    return tl_GetOpenFileNameA(open_filename);
}

TL_COMDLG_MSABI int tl_GetSaveFileNameW(void* open_filename) noexcept {
    return tl_GetOpenFileNameW(open_filename);
}

TL_COMDLG_MSABI int tl_ChooseColorA(void* choose_color) noexcept {
    (void)choose_color;
    return 1;
}

TL_COMDLG_MSABI int tl_ChooseColorW(void* choose_color) noexcept {
    (void)choose_color;
    return 1;
}

}  // extern "C"

}  // namespace tradutorlinux
