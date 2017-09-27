#ifndef AFINA_ALLOCATOR_DESCRIPTOR_H
#define AFINA_ALLOCATOR_DESCRIPTOR_H
#include <cstddef>
#include <string>

namespace Afina {
namespace Allocator {

struct Descriptor {
  void *ptr;
  size_t size;
  Descriptor() : ptr(nullptr), size(0) {}
  Descriptor(void *p, size_t s) : ptr(p), size(s) {}
  Descriptor *add_descriptor(int *descriptor_count, bool &is_new_space);
  static Descriptor *find_descriptor_addr(int *descriptor_count,
                                          void *descriptor_ptr);
};

} // namespace Afina
} // namespace Allocator
#endif // AFINA_ALLOCATOR_DESCRIPTOR_H
