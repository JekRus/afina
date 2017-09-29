#include <afina/allocator/Pointer.h>

namespace Afina {
namespace Allocator {

Pointer::Pointer() : descriptor(nullptr) {}
Pointer::Pointer(Descriptor *d) : descriptor(d) {}
Pointer::Pointer(const Pointer &ptr) { descriptor = ptr.descriptor; }

Pointer::Pointer(Pointer &&ptr) {
    descriptor = ptr.descriptor;
    ptr.descriptor = nullptr;
}

Pointer &Pointer::operator=(const Pointer &ptr) {
    descriptor = ptr.descriptor;
    return *this;
}

Pointer &Pointer::operator=(Pointer &&ptr) {
    Descriptor *tmp = this->descriptor;
    this->descriptor = ptr.descriptor;
    ptr.descriptor = tmp;
    return *this;
}

Pointer::~Pointer() { descriptor = nullptr; }

void *Pointer::get() const {
    if (descriptor != nullptr) {
        return descriptor->get_ptr();
    } else {
        return nullptr;
    }
}

size_t Pointer::getsize() const {
    if (descriptor != nullptr) {
        return descriptor->get_size();
    } else {
        return 0;
    }
}

void Pointer::clear() {
    if (descriptor != nullptr) {
        descriptor->set_ptr(nullptr);
        descriptor->set_size(0);
    }
}

} // namespace Allocator
} // namespace Afina
