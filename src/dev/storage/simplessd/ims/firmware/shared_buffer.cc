#include "shared_buffer.hh"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

uint8_t* g_metadata_buffer = nullptr;
size_t g_metadata_buffer_size = 0;

void init_metadata_buffer(size_t size) {
    if (g_metadata_buffer) std::free(g_metadata_buffer); // 釋放舊的
    g_metadata_buffer = static_cast<uint8_t*>(std::malloc(size));
    if (!g_metadata_buffer) throw std::bad_alloc();
    g_metadata_buffer_size = size;
}

int write_metadata(uint8_t* data, size_t size) {
    if (!g_metadata_buffer || size > g_metadata_buffer_size) {
        return -1; // buffer 尚未初始化或太小
    }
    std::memcpy(g_metadata_buffer, data, size);
    return 0;
}
