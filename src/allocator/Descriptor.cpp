#include <afina/allocator/Descriptor.h>

namespace Afina {
namespace Allocator {

Descriptor *Descriptor::add_descriptor(int *descriptor_count, bool &is_new_space) {
    Descriptor *p = reinterpret_cast<Descriptor *>(descriptor_count) - 1;
    int k = 0;
    while ((k < *descriptor_count) && (p->ptr != nullptr)) {
        k++;
        p--;
    }
    if (k == *descriptor_count) {
        (*descriptor_count)++;
        p--;
        is_new_space = true;
    }
    *p = *this;
    return p;
}

Descriptor *Descriptor::find_descriptor_addr(int *descriptor_count, void *descriptor_ptr) {
    Descriptor *p = reinterpret_cast<Descriptor *>(descriptor_count) - 1;
    int k = 0;
    while ((k < *descriptor_count) && (p->ptr != descriptor_ptr)) {
        k++;
        p--;
    }
    if (p->ptr == descriptor_ptr) {
        return p;
    } else {
        return nullptr;
    }
}

} // namespace Allocator
} // namespace Afina
