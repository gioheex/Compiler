#include "compiler.h"
#include "helpers/vector.h"
#include <assert.h>

struct vector* node_vector = NULL;
struct vector* node_vector_root = NULL;

void node_set_vector(struct vector* vec, struct vector* root_vec) {
    node_vector = vec;
    node_vector_root = root_vec;
}

// Adiciona o node ao final da lista.
void node_push(struct node* node) {
    vector_push(node_vector, &node);
}

// Pega o node da parte de trás do vetor. Se nao tiver nada, retorna NULL.
struct node* node_peek_or_null() {
    return vector_back_ptr_or_null(node_vector);
}

struct node* node_peek() {
    return *(struct node**) (vector_back(node_vector));
}

struct node* node_pop() {
    struct node* last_node = vector_back_ptr(node_vector);
    struct node* last_node_root = vector_empty(node_vector) ? NULL : vector_back_ptr(node_vector_root);

    vector_pop(node_vector);

    if (last_node == last_node_root) vector_pop(node_vector_root);

    return last_node;
}
