#include <afina/allocator/Descriptor.h>
#include <afina/allocator/Error.h>
#include <afina/allocator/List.h>
#include <afina/allocator/Pointer.h>
#include <afina/allocator/Simple.h>
#include <cstring>

namespace Afina {
namespace Allocator {

Simple::Simple(void *base, size_t size) : _base(base), _base_len(size) {
    auto *p = static_cast<uint8_t *>(_base) + _base_len;
    List freespace_list;
    Node head;
    Node first_node{size - sizeof(List) - sizeof(int) - sizeof(Descriptor) - 2 * sizeof(Node), nullptr};
    Node *node_p = static_cast<Node *>(_base);
    // 1-st node (real)
    *node_p = first_node;
    head.next = node_p;
    // 1-st node(fictive)
    *(reinterpret_cast<Node *>(p - sizeof(List) - sizeof(Node))) = head;
    freespace_list.set_head(reinterpret_cast<Node *>(p - sizeof(List) - sizeof(Node)));
    // list
    *(reinterpret_cast<List *>(p - sizeof(List))) = freespace_list;
    // descriptor_counter
    *(reinterpret_cast<int *>(p - sizeof(List) - sizeof(Node) - sizeof(int))) = 0;
}

Pointer Simple::alloc(size_t N) {
    void *descriptor_count =
        static_cast<void *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List) - sizeof(Node) - sizeof(int)));
    List *lst_p = reinterpret_cast<List *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List)));

    if (N < sizeof(Node)) {
        N = sizeof(Node);
    }
    if (lst_p->get_head()->next == nullptr) {
        throw AllocError(AllocErrorType::NoMemory, std::string("not enough memory\n"));
    }
    Node *node_p = lst_p->search_greater(N);
    if (node_p == nullptr) {
        defrag();
        node_p = lst_p->search_greater(N);
        if (node_p == nullptr) {
            throw AllocError(AllocErrorType::NoMemory, std::string("not enough memory\n"));
        }
    }

    bool is_extra_space = false;
    Descriptor descriptor(nullptr, N);
    Descriptor *descriptor_addr;
    size_t old_node_size = node_p->next->size;
    descriptor.set_ptr(static_cast<void *>(node_p->next));
    descriptor_addr = descriptor.add_descriptor(static_cast<int *>(descriptor_count), is_extra_space);

    if (old_node_size >= sizeof(Node) + N) {
        Node *new_node_p = reinterpret_cast<Node *>(reinterpret_cast<uint8_t *>(node_p->next) + N);
        lst_p->delete_node(node_p);
        lst_p->add_node(node_p, new_node_p);
        new_node_p->size = old_node_size - N;
    } else {
        lst_p->delete_node(node_p);
    }
    //корректировка правой границы памяти
    if (is_extra_space) {
        lst_p->correct_border();
    }
    return Pointer(descriptor_addr);
}

void Simple::realloc(Pointer &p, size_t N) {
    if (p.get() == nullptr) {
        p = this->alloc(N);
        return;
    }
    if (N < sizeof(Node)) {
        N = sizeof(Node);
    }
    List *lst_p = reinterpret_cast<List *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List)));
    void *descriptor_ptr = p.get();
    size_t old_size = p.getsize();
    void *mem_end_ptr = static_cast<uint8_t *>(descriptor_ptr) + old_size;
    bool replace = false;

    Node *node_p = lst_p->get_head();
    while ((node_p->next != nullptr) && (static_cast<void *>(node_p->next) != mem_end_ptr)) {
        node_p = node_p->next;
    }
    if (static_cast<void *>(node_p->next) == mem_end_ptr) {
        if (node_p->next->size + old_size >= N + sizeof(Node)) {
            Node new_node{node_p->next->size + old_size - N, node_p->next->next};
            node_p->next = reinterpret_cast<Node *>(reinterpret_cast<uint8_t *>(node_p->next) - old_size + N);
            *(node_p->next) = new_node;
            return;
        } else if (node_p->next->size + old_size >= N) {
            lst_p->delete_node(node_p);
            return;
        } else {
            replace = true;
        }
    } else { // node_p -> next == nullptr
        replace = true;
    }
    if (replace) {
        void *tmp = p.get();
        Pointer q = this->alloc(N);
        std::memmove(q.get(), tmp, old_size);
        this->free(p);
        p = q;
    }
    return;
}

void Simple::free(Pointer &p) {
    List *lst_p = reinterpret_cast<List *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List)));
    void *descriptor_ptr = p.get();
    if (descriptor_ptr == nullptr) {
        throw(AllocError(AllocErrorType::InvalidFree, "invalid free on nullptr\n"));
    }
    *(static_cast<Node *>(descriptor_ptr)) = Node{p.getsize(), nullptr};
    Node *node_p = lst_p->get_head();
    while ((node_p->next != nullptr) && (node_p->next < static_cast<Node *>(descriptor_ptr))) {
        node_p = node_p->next;
    }
    lst_p->add_node(node_p, static_cast<Node *>(descriptor_ptr));
    if (lst_p->is_close_nodes(node_p, node_p->next)) {
        lst_p->merge_nodes(node_p, node_p->next);
    }
    node_p = node_p->next;
    if ((node_p->next != nullptr) && (lst_p->is_close_nodes(node_p, node_p->next))) {
        lst_p->merge_nodes(node_p, node_p->next);
    }
    p.clear();
}

void Simple::defrag() {
    void *descriptor_count =
        static_cast<void *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List) - sizeof(Node) - sizeof(int)));
    List *lst_p = reinterpret_cast<List *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List)));
    auto &node_p = lst_p->get_head()->next;
    if (node_p == nullptr) {
        return;
    }
    while (node_p->next != nullptr) {
        if (lst_p->is_close_nodes(node_p, node_p->next)) {
            lst_p->merge_nodes(node_p, node_p->next);
        } else {
            void *p = reinterpret_cast<uint8_t *>(node_p) + node_p->size;
            Descriptor *descriptor_addr = Descriptor::find_descriptor(static_cast<int *>(descriptor_count), p);
            if (descriptor_addr != nullptr) {
                Node tmp = *node_p;
                std::memmove(node_p, descriptor_addr->get_ptr(), descriptor_addr->get_size());
                descriptor_addr->set_ptr(node_p);
                node_p = reinterpret_cast<Node *>(static_cast<uint8_t *>(descriptor_addr->get_ptr()) +
                                                  descriptor_addr->get_size());
                *node_p = tmp;
            } else {
                return;
            }
        }
    }
    void *p = reinterpret_cast<uint8_t *>(node_p) + node_p->size;
    Descriptor *descriptor_addr = Descriptor::find_descriptor(static_cast<int *>(descriptor_count), p);
    while (descriptor_addr != nullptr) {
        Node tmp = *node_p;
        std::memmove(node_p, descriptor_addr->get_ptr(), descriptor_addr->get_size());
        descriptor_addr->set_ptr(node_p);
        node_p =
            reinterpret_cast<Node *>(static_cast<uint8_t *>(descriptor_addr->get_ptr()) + descriptor_addr->get_size());
        *node_p = tmp;
        p = reinterpret_cast<uint8_t *>(node_p) + node_p->size;
        descriptor_addr = Descriptor::find_descriptor(static_cast<int *>(descriptor_count), p);
    }
    return;
}

std::string Simple::dump() const { return ""; }

} // namespace Allocator
} // namespace Afina
