#include <cstdint>
#include <limits>

#include <zlib.h>

extern "C" std::int32_t tl_msix_inflate_raw(
    const std::uint8_t* const input,
    const std::uint64_t input_length,
    std::uint8_t* const output,
    const std::uint64_t output_length) noexcept {
    if (input_length > static_cast<std::uint64_t>(std::numeric_limits<uInt>::max()) ||
        output_length > static_cast<std::uint64_t>(std::numeric_limits<uInt>::max()) ||
        (input_length != 0 && input == nullptr) || (output_length != 0 && output == nullptr)) {
        return -2;
    }

    std::uint8_t empty_output = 0;
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input));
    stream.avail_in = static_cast<uInt>(input_length);
    stream.next_out = output_length == 0
                          ? &empty_output
                          : reinterpret_cast<Bytef*>(output);
    stream.avail_out = static_cast<uInt>(output_length);
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return -3;
    const int result = inflate(&stream, Z_FINISH);
    const bool valid = result == Z_STREAM_END && stream.avail_in == 0 &&
                       stream.total_out == output_length;
    inflateEnd(&stream);
    return valid ? 0 : -1;
}
