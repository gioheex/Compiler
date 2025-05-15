#include "compiler.h"
#include "helpers/vector.h"

static struct compile_process* current_process;
static struct token* parser_last_token;

// Estrutura para passar comandos atraves de funcoes recursivas.
struct history {
    int flags;
};

void parse_expressionable(struct history* history);
int parse_expressionable_single(struct history* history);

int parse_next(){
    return 0;
}

struct history* history_begin(int flags) {
    struct history* history = calloc(1, sizeof(struct history));
    history->flags = flags;
    return history;
}

struct history* history_down(struct history* history, int flags) {
    struct history* new_history = calloc(1, sizeof(struct history));
    memcpy(new_history, history, sizeof(struct history));
    new_history->flags = flags;
    return new_history;
}

int parse(struct compile_process* process) {                /*LAB3: Adicionar o prototipo no compiler.h */
    struct node* node = NULL;
    current_process = process;
    
    vector_set_peek_pointer(process->token_vec, 0);

    while (parse_next() == 0) {
        //node = node_peek();
        vector_push(process->node_tree_vec, &node);
    }
    return PARSE_ALL_OK;
}

