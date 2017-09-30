#ifndef AFINA_ALLOCATOR_DESCRIPTOR_H
#define AFINA_ALLOCATOR_DESCRIPTOR_H
#include <cstddef>
#include <string>

namespace Afina {
namespace Allocator {

class Descriptor {
public:
    Descriptor() : ptr(nullptr), size(0) {}
    Descriptor(void *p, size_t s) : ptr(p), size(s) {}

    Descriptor *add_descriptor(int *descriptor_count, bool &is_new_space);
    static Descriptor *find_descriptor(int *descriptor_count, void *descriptor_ptr);
    void *get_ptr() const;
    size_t get_size() const;
    void set_ptr(void *p);
    void set_size(size_t s);

private:
    void *ptr;
    size_t size;
};

} // namespace Allocator
} // namespace Afina
#endif // AFINA_ALLOCATOR_DESCRIPTOR_H
