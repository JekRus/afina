#include <afina/allocator/List.h>

namespace Afina {
namespace Allocator {

Node *List::search_greater(size_t alloc_size) {
    Node *p = head->next;
    if(p == nullptr) {
		return nullptr;
	}
	else if(p->size >= alloc_size) {
		return head;
	} 
    while (p->next != nullptr) {
        if (p->next->size >= alloc_size) {
			return p;
        }
        p = p->next;
    }
    return nullptr;
}

void List::delete_node(Node *prev_node) {
    if ((prev_node == nullptr) || ((prev_node->next) == nullptr)) {
        throw std::string("error at List::delete_node\n");
    }
    prev_node->next = prev_node->next->next;
}

void List::add_node(Node *prev_node, Node *new_node) {
    if ((prev_node == nullptr) || (new_node == nullptr)) {
        throw std::string("error at List::add_node\n");
    }
    new_node->next = prev_node->next;
    prev_node->next = new_node;
}

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

void List::correct_border() {
    Node *p = head;
    if(p == nullptr) {
		throw(std::string("error at List::correct_border\n"));
	}
	if(p->next == nullptr) {
		return;
	}
    while (p->next->next != nullptr) {
        p++;
    }
    if (p->next->size >= sizeof(Node) + sizeof(Descriptor)) {
        p->next->size -= sizeof(Descriptor);
    } else {
        p->next = nullptr;
    }
}

} // namespace Allocator
} // namespace Afina
