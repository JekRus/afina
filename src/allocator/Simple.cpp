#include <afina/allocator/Descriptor.h>
#include <afina/allocator/Error.h>
#include <afina/allocator/List.h>
#include <afina/allocator/Pointer.h>
#include <afina/allocator/Simple.h>
#include <cstring>

namespace Afina {
namespace Allocator {

Simple::Simple(void *base, size_t size) : _base(base), _base_len(size) {
    Node first_node{size - sizeof(List) - sizeof(int) - sizeof(Descriptor) - sizeof(Node), nullptr};
    Node *node_p = static_cast<Node *>(_base);
    *node_p = first_node;
    List freespace_list(node_p);
    auto p = static_cast<uint8_t *>(_base);
    *(reinterpret_cast<List *>(p + (_base_len - sizeof(List)))) = freespace_list;
    *(reinterpret_cast<int *>(p + (_base_len - sizeof(List) - sizeof(int)))) = 0;
}

Pointer Simple::alloc(size_t N) {
    void *descriptor_count =
        static_cast<void *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List) - sizeof(int)));
    List *lst_p = reinterpret_cast<List *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List)));
    if (N < sizeof(Node)) {
        N = sizeof(Node);
    }
    if (lst_p->head == nullptr) {
        throw AllocError(AllocErrorType::NoMemory, std::string("not enough memory\n"));
    }
    void *p = lst_p->search_greater(N);
    if (p == nullptr) {
        defrag();
        p = lst_p->search_greater(N);
        if (p == nullptr) {
            throw AllocError(AllocErrorType::NoMemory, std::string("not enough memory\n"));
        }
    }
    Node *head = lst_p->head;
    bool is_new_space = false;
    Descriptor descriptor{nullptr, N};
    Descriptor *descriptor_addr;
    // 2 частных случая: первое звено списка либо любое другое
    if (reinterpret_cast<List *>(p) == lst_p) {
        descriptor.ptr = static_cast<void *>(head);
        descriptor_addr = descriptor.add_descriptor(static_cast<int *>(descriptor_count), is_new_space);
        auto old_node_size = head->size;
        if (old_node_size >= sizeof(Node) + N) {
            Node *new_node_p = reinterpret_cast<Node *>(reinterpret_cast<uint8_t *>(head) + N);
            lst_p->delete_node(nullptr, true);
            new_node_p->size = old_node_size - N;
            lst_p->add_node(nullptr, new_node_p, true);
        } else {
            lst_p->delete_node(nullptr, true);
        }
    } else {
        auto node_p = static_cast<Node *>(p);
        auto old_node_size = node_p->next->size;
        descriptor.ptr = static_cast<void *>(node_p->next);
        descriptor_addr = descriptor.add_descriptor(static_cast<int *>(descriptor_count), is_new_space);
        if (old_node_size >= sizeof(Node) + N) {
            Node *new_node_p = reinterpret_cast<Node *>(reinterpret_cast<uint8_t *>(node_p->next) + N);
            lst_p->delete_node(node_p);
            lst_p->add_node(node_p, new_node_p);
            new_node_p->size = old_node_size - N;
        } else {
            lst_p->delete_node(node_p);
        }
    }
    //корректировка правой границы памяти
    if (is_new_space) {
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
    auto &head = lst_p->head;
    void *descriptor_ptr = p.get();
    size_t old_size = p.getsize();
    void *mem_end_ptr = static_cast<uint8_t *>(descriptor_ptr) + old_size;
    bool replace = false;

    if (mem_end_ptr == static_cast<void *>(head)) {
        if (head->size + old_size >= N + sizeof(Node)) {
            Node new_node{head->size + old_size - N, head->next};
            head = reinterpret_cast<Node *>(reinterpret_cast<uint8_t *>(head) - old_size + N);
            *head = new_node;
            return;
        } else if (head->size + old_size >= N) {
            lst_p->delete_node(nullptr, true);
            return;
        } else {
            replace = true;
        }
    } else {
        Node *node_p = head;
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
                lst_p->delete_node(node_p, false);
                return;
            } else {
                replace = true;
            }
        } else { // node_p -> next == nullptr
            replace = true;
        }
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
    auto &head = lst_p->head;
    void *descriptor_ptr = p.get();
    if (descriptor_ptr == nullptr) {
        throw(AllocError(AllocErrorType::InvalidFree, "invalid free on nullptr\n"));
    }
    if ((head == nullptr) || (static_cast<Node *>(descriptor_ptr) < head)) {

        *(static_cast<Node *>(descriptor_ptr)) = Node{p.getsize(), head};
        head = static_cast<Node *>(descriptor_ptr);
        if ((head->next != nullptr) && (lst_p->is_close_nodes(head, head->next))) {
            lst_p->merge_nodes(head, head->next);
        }
    } else {
        *(static_cast<Node *>(descriptor_ptr)) = Node{p.getsize(), nullptr};
        Node *q = lst_p->head;
        while ((q->next != nullptr) && (q->next < static_cast<Node *>(descriptor_ptr))) {
            q = q->next;
        }
        lst_p->add_node(q, static_cast<Node *>(descriptor_ptr));
        if (lst_p->is_close_nodes(q, q->next)) {
            lst_p->merge_nodes(q, q->next);
        }
        q = q->next;
        if ((q->next != nullptr) && (lst_p->is_close_nodes(q, q->next))) {
            lst_p->merge_nodes(q, q->next);
        }
    }
    p.clear();
}

void Simple::defrag() {
    void *descriptor_count =
        static_cast<void *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List) - sizeof(int)));
    List *lst_p = reinterpret_cast<List *>(static_cast<uint8_t *>(_base) + (_base_len - sizeof(List)));
    auto &head = lst_p->head;
    if (head == nullptr) {
        return;
    }
    while (head->next != nullptr) {
        if (lst_p->is_close_nodes(head, head->next)) {
            lst_p->merge_nodes(head, head->next);
        } else {
            void *p = reinterpret_cast<uint8_t *>(head) + head->size;
            Descriptor *descriptor_addr = Descriptor::find_descriptor_addr(static_cast<int *>(descriptor_count), p);
            if (descriptor_addr != nullptr) {
                Node tmp = *head;
                std::memmove(head, descriptor_addr->ptr, descriptor_addr->size);
                descriptor_addr->ptr = head;
                head = reinterpret_cast<Node *>(static_cast<uint8_t *>(descriptor_addr->ptr) + descriptor_addr->size);
                *head = tmp;
            } else {
                return;
            }
        }
    }
    void *p = reinterpret_cast<uint8_t *>(head) + head->size;
    Descriptor *descriptor_addr = Descriptor::find_descriptor_addr(static_cast<int *>(descriptor_count), p);
    while (descriptor_addr != nullptr) {
        Node tmp = *head;
        std::memmove(head, descriptor_addr->ptr, descriptor_addr->size);
        descriptor_addr->ptr = head;
        head = reinterpret_cast<Node *>(static_cast<uint8_t *>(descriptor_addr->ptr) + descriptor_addr->size);
        *head = tmp;
        p = reinterpret_cast<uint8_t *>(head) + head->size;
        descriptor_addr = Descriptor::find_descriptor_addr(static_cast<int *>(descriptor_count), p);
    }
    return;
}

std::string Simple::dump() const { return ""; }

} // namespace Allocator
} // namespace Afina
