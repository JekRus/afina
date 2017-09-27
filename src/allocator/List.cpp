#include <afina/allocator/List.h>

namespace Afina {
namespace Allocator {

bool List::is_close_nodes(Node *first_node, Node *second_node) const {
    if (first_node == nullptr || second_node == nullptr) {
        throw(std::string("error at List::merge_nodes\n"));
    }
    if (reinterpret_cast<uint8_t *>(first_node) + first_node->size == reinterpret_cast<uint8_t *>(second_node)) {
        return true;
    }
    return false;
}

void List::merge_nodes(Node *first_node, Node *second_node) {
    if (first_node == nullptr || second_node == nullptr) {
        throw(std::string("error at List::merge_nodes\n"));
    }
    first_node->size += second_node->size;
    first_node->next = second_node->next;
}
// returns pointer on previous
// node (p -> next -> size >= alloc_size) or pointer on the head
void *List::search_greater(size_t alloc_size) {
    if (head == nullptr) {
        return nullptr;
    }
    if (head->size >= alloc_size) {
        return this;
    }
    Node *p = head;
    while (p->next != nullptr) {
        if (p->next->size >= alloc_size) {
            return p;
        }
        p = p->next;
    }
    return nullptr;
}

void List::delete_node(Node *prev_node, bool is_first) {
    if (is_first) {
        head = head->next;
    } else {
        if ((prev_node == nullptr) || ((prev_node->next) == nullptr)) {
            throw std::string("error at List::delete_node\n");
        }
        prev_node->next = prev_node->next->next;
    }
}

void List::add_node(Node *prev_node, Node *new_node, bool is_first) {
    if (is_first) { // head
        new_node->next = this->head;
        this->head = new_node;
    } else {
        if ((prev_node == nullptr) || (new_node == nullptr)) {
            throw std::string("error at List::add_node\n");
        }
        new_node->next = prev_node->next;
        prev_node->next = new_node;
    }
}

void List::correct_border() {
    if (this->head != nullptr) {
        auto q = this->head;
        if (q->next == nullptr) {
            if (q->size >= sizeof(Node) + sizeof(Descriptor)) {
                q->size -= sizeof(Descriptor);
            } else {
                this->head = nullptr;
            }
        } else {
            while (q->next->next != nullptr) {
                q++;
            }
            if (q->next->size >= sizeof(Node) + sizeof(Descriptor)) {
                q->next->size -= sizeof(Descriptor);
            } else {
                q->next = nullptr;
            }
        }
    }
}

} // namespace Allocator
} // namespace Afina
