#ifndef AFINA_ALLOCATOR_POINTER_H
#define AFINA_ALLOCATOR_POINTER_H
#include <afina/allocator/Descriptor.h>
#include <cstddef>

namespace Afina {
namespace Allocator {
// Forward declaration. Do not include real class definition
// to avoid expensive macros calculations and increase compile speed
class Simple;

class Pointer {
public:
    Pointer();
    Pointer(Descriptor *);

    Pointer(const Pointer &);
    Pointer(Pointer &&);

    Pointer &operator=(const Pointer &);
    Pointer &operator=(Pointer &&);

    ~Pointer();

    void *get() const;
    size_t getsize() const;
    void clear();

private:
    Descriptor *descriptor;
};

} // namespace Allocator
} // namespace Afina

#endif // AFINA_ALLOCATOR_POINTER_H
