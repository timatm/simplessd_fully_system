#ifndef __SHARED__BUFFER_HH__
#define __SHARED__BUFFER_HH__
#include <cstddef>
#include <cstdint>

extern uint8_t* g_metadata_buffer;
extern size_t g_metadata_buffer_size;

// 初始化 buffer
void init_metadata_buffer(size_t size);

// 寫入 metadata
int write_metadata(uint8_t* data, size_t size);


#endif  // __SHARED__BUFFER_HH__
