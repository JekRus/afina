#ifndef AFINA_ALLOCATOR_LIST_H
#define AFINA_ALLOCATOR_LIST_H

#include <cstddef>
#include <string>

namespace Afina {
namespace Allocator {

struct Node {
	Node() : size(0), next(nullptr) {}
	Node(size_t s, Node *p) : size(s), next(p) {}

    size_t size;
    Node *next;
};

class List {
public:
    List(Node *p = nullptr) : head(p) {}
    Node *search_greater(size_t alloc_size);
    void delete_node(Node *prev_node);
    void add_node(Node *prev_node, Node *new_node);
    void correct_border();
    bool is_close_nodes(Node *first_node, Node *second_node) const;
    void merge_nodes(Node *first_node, Node *second_node);
    Node *get_head() const;
    void set_head(Node *h);

private:
    Node *head;
};

} // namespace Afina
} // namespace Allocator
#endif // AFINA_ALLOCATOR_LIST_H
