#include "compiler.h"
#include "helpers/vector.h"
#include <assert.h>

struct history
{
    int flags;
};

int parse(struct compile_process *process);
static struct compile_process *current_process;
static struct token *parser_last_token;
void parse_expressionable(struct history *history);
int parse_expressionable_single(struct history *history);
static bool parser_left_op_has_priority(const char *op_left, const char *op_right);
extern struct expressionable_op_precedence_group op_precedence[TOTAL_OPERADOR_GROUPS];
void parse_identifier(struct history *history);
static bool keyword_is_datatype(const char *val);
static bool is_keyword_variable_modifier(const char *val);
bool token_is_primitive_keyword(struct token *token);
void parser_get_datatype_tokens(struct token **datatype_token, struct token **datatype_secundary_token);
int parser_datatype_expected_for_type_string(const char *str);
int parser_get_pointer_depth();
bool parser_datatype_is_secondary_allowed_for_type(const char *type);
void parser_datatype_adjust_size_for_secondary(struct datatype *datatype, struct token *datatype_secondary_token);
void parser_datatype_init_type_and_size_for_primitive(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out);
void parser_datatype_init_type_and_size(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out, int pointer_depth, int expected_type);
void parser_datatype_init(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out, int pointer_depth, int expected_type);
void parse_datatype_type(struct datatype *dtype);
void parse_datatype_modifiers(struct datatype *dtype);
void parse_datatype(struct datatype *dtype);
void parse_if(struct history *history);
void parse_expressionable_root(struct history *history);
void make_variable_node_and_register(struct history *history, struct datatype *dtype, struct token *name_token, struct node *value_node);
void make_variable_node(struct datatype *dtype, struct token *name_token, struct node *value_node);
void parse_variable(struct datatype *dtype, struct token *name_token, struct history *history);
void parse_variable_function_or_struct_union(struct history *history);
void parse_keyword(struct history *history);

struct history *history_begin(int flags)
{
    struct history *history = calloc(1, sizeof(struct history));
    history->flags = flags;
    return history;
}
struct history *history_down(struct history *history, int flags)
{
    struct history *new_history = calloc(1, sizeof(struct history));
    memcpy(new_history, history, sizeof(struct history));
    new_history->flags = flags;
    return new_history;
}

static void parser_ignore_nl_or_comment(struct token *token)
{
    while (token && discart_token(token))
    {
        vector_peek(current_process->token_vec);
        token = vector_peek_no_increment(current_process->token_vec);
    }
}

static struct token *token_next()
{
    struct token *next_token = vector_peek_no_increment(current_process->token_vec);
    parser_ignore_nl_or_comment(next_token);
    current_process->pos = next_token->pos; // Atualiza a posicao do arquivo de compilacao.
    parser_last_token = next_token;
    return vector_peek(current_process->token_vec);
}

static struct token *token_peek_next()
{
    struct token *next_token = vector_peek_no_increment(current_process->token_vec);
    parser_ignore_nl_or_comment(next_token);
    return vector_peek_no_increment(current_process->token_vec);
}

void parse_single_token_to_node()
{
    struct token *token = token_next();
    struct node *node = NULL;
    switch (token->type)
    {
    case TOKEN_TYPE_NUMBER:
        node = node_create(&(struct node){.type = NODE_TYPE_NUMBER, .llnum = token->llnum});
        break;
    case TOKEN_TYPE_IDENTIFIER:
        node = node_create(&(struct node){.type = NODE_TYPE_IDENTIFIER, .sval = token->sval});
        break;
    case TOKEN_TYPE_STRING:
        node = node_create(&(struct node){.type = NODE_TYPE_STRING, .sval = token->sval});
        break;
    default:
        compiler_error(current_process, "Esse token nao pode ser convertido para node!\n");
        break;
    }

    // Verifica se o nó foi criado com sucesso
    if (node)
    {
        node_push(node);
    }
    else
    {
        printf("ERRO: Falha ao criar nó para token: %d\n", token->type);
    }
}

void parse_expressionable_for_op(struct history *history, const char *op)
{
    if (!op)
        return;
    parse_expressionable(history);
}

void parser_node_shift_children_left(struct node *node)
{
    assert(node->type == NODE_TYPE_EXPRESSION);
    assert(node->exp.right == NODE_TYPE_EXPRESSION);

    const char *right_op = node->exp.right->exp.op;
    struct node *new_exp_left_node = node->exp.left;
    struct node *new_exp_right_node = node->exp.right->exp.left;
    make_exp_node(new_exp_left_node, new_exp_right_node, node->exp.op);

    // EX: 50*E(20+50) -> E(50*20)+50
    struct node *new_left_operand = node_pop();
    struct node *new_right_operand = node->exp.right->exp.right;
    node->exp.left = new_left_operand;
    node->exp.right = new_right_operand;
    node->exp.op = right_op;
}

void parser_reorder_expression(struct node **node_out)
{
    struct node *node = *node_out;

    // Only reorder expression nodes
    if (!node || node->type != NODE_TYPE_EXPRESSION)
        return;

    // Recurse into sub-expressions first
    if (node->exp.left && node->exp.left->type == NODE_TYPE_EXPRESSION)
        parser_reorder_expression(&node->exp.left);

    if (node->exp.right && node->exp.right->type == NODE_TYPE_EXPRESSION)
        parser_reorder_expression(&node->exp.right);

    // Now check if the right expression has higher precedence
    if (node->exp.right && node->exp.right->type == NODE_TYPE_EXPRESSION)
    {
        const char *current_op = node->exp.op;
        const char *right_op = node->exp.right->exp.op;

        if (parser_left_op_has_priority(current_op, right_op))
        {
            // Get nodes A, B, C
            struct node *A = node->exp.left;
            struct node *B = node->exp.right->exp.left;
            struct node *C = node->exp.right->exp.right;

            // Save the operator from the right expression
            const char *new_op = right_op;

            // Create (A op B) subtree
            make_exp_node(A, B, current_op);
            struct node *new_left = node_pop();

            // Update current node: (A op B) new_op C
            node->exp.left = new_left;
            node->exp.right = C;
            node->exp.op = new_op;

            // Check if we need to further reorder the new left subtree
            parser_reorder_expression(&node->exp.left);
        }
    }
}

static int parser_get_precedence_for_operator(const char *op, struct expressionable_op_precedence_group **group_out)
{
    *group_out = NULL;
    for (int i = 0; i < TOTAL_OPERADOR_GROUPS; i++)
    {
        for (int j = 0; op_precedence[i].operators[j]; j++)
        {
            const char *_op = op_precedence[i].operators[j];
            if (S_EQ(op, _op))
            {
                *group_out = &op_precedence[i];
                return i;
            }
        }
    }
    return -1;
}

static bool parser_left_op_has_priority(const char *op_left, const char *op_right)
{
    struct expressionable_op_precedence_group *group_left = NULL;
    struct expressionable_op_precedence_group *group_right = NULL;

    int precedence_left = parser_get_precedence_for_operator(op_left, &group_left);
    int precedence_right = parser_get_precedence_for_operator(op_right, &group_right);

    // Compara os precedentes
    if (precedence_left != precedence_right)
    {
        return precedence_left < precedence_right;
    }

    // Igual -> Confere associatividade
    if (group_left && group_left->associativity == ASSOCIATIVITY_LEFT_TO_RIGTH)
    {
        return false;
    }

    // Associatividade direita -> esquerda prioridade
    if (group_left && group_left->associativity == ASSOCIATIVITY_RIGHT_TO_LEFT)
    {
        return true;
    }

    return false;
}

void parse_exp_normal(struct history *history)
{
    struct token *op_token = token_peek_next();
    if (!op_token)
        return;
    const char *op = op_token->sval;
    if (!op)
        return;

    struct node *node_left = node_peek_expressionable_or_null();
    if (!node_left)
        return;

    // Retira da lista de tokens o token de operador. Ex: "123+456", retira o token "+".
    token_next();

    // Retira da lista de nodes, o ultimo node inserido.
    node_pop();

    node_left->flags |= NODE_FLAG_INSIDE_EXPRESSION;

    // Nesse momento, temos o node da esquerda e o operador. Essa funcao ira criar o node da direita.
    parse_expressionable_for_op(history_down(history, history->flags), op);
    struct node *node_right = node_pop();
    node_right->flags |= NODE_FLAG_INSIDE_EXPRESSION;

    // Cria o node de expressao, passando o node da esquerda, node da direita e operador.
    make_exp_node(node_left, node_right, op);
    struct node *exp_node = node_pop();

    // TODO: Inserir AQUI codigo para reordenador a expressao.
    parser_reorder_expression(&exp_node); // LAB4

    node_push(exp_node);
}

int parse_exp(struct history *history)
{
    parse_exp_normal(history);
    return 0;
}

void parse_keyword_for_global()
{
    parse_keyword(history_begin(0));
    // TODO: Essa funcao ainda nao cria o node corretamente.
    struct node *node = node_pop();
}

void parse_identifier(struct history *history)
{
    struct token *token = token_peek_next();
    if (!token || token->type != TOKEN_TYPE_IDENTIFIER)
    {
        compiler_error(current_process, "Esperado identificador\n");
        return;
    }
    parse_single_token_to_node();
}

static bool keyword_is_datatype(const char *val)
{ // LAB5
    return S_EQ(val, "void") ||
           S_EQ(val, "char") ||
           S_EQ(val, "int") ||
           S_EQ(val, "short") ||
           S_EQ(val, "float") ||
           S_EQ(val, "double") ||
           S_EQ(val, "long") ||
           S_EQ(val, "struct") ||
           S_EQ(val, "union");
}

static bool is_keyword_variable_modifier(const char *val)
{ // LAB5
    return S_EQ(val, "unsigned") ||
           S_EQ(val, "signed") ||
           S_EQ(val, "static") ||
           S_EQ(val, "const") ||
           S_EQ(val, "extern") ||
           S_EQ(val, "__ignore_typecheck__");
}

bool token_is_primitive_keyword(struct token *token)
{ // LAB5
    if (!token)
        return false;
    if (token->type != TOKEN_TYPE_KEYWORD)
        return false;
    if (S_EQ(token->sval, "void"))
    {
        return true;
    }
    else if (S_EQ(token->sval, "char"))
    {
        return true;
    }
    else if (S_EQ(token->sval, "short"))
    {
        return true;
    }
    else if (S_EQ(token->sval, "int"))
    {
        return true;
    }
    else if (S_EQ(token->sval, "long"))
    {
        return true;
    }
    else if (S_EQ(token->sval, "float"))
    {
        return true;
    }
    else if (S_EQ(token->sval, "double"))
    {
        return true;
    }
    return false;
}

// Essa funcao trata o caso de 2 datatypes seguidos. Ex: long int A.
void parser_get_datatype_tokens(struct token **datatype_token, struct token **datatype_secundary_token)
{ // LAB5
    *datatype_token = token_next();
    struct token *next_token = token_peek_next();
    if (token_is_primitive_keyword(next_token))
    {
        *datatype_secundary_token = next_token;
        token_next();
    }
}

int parser_datatype_expected_for_type_string(const char *str)
{ // LAB5
    int type = DATATYPE_EXPECT_PRIMITIVE;
    if (S_EQ(str, "union"))
    {
        type = DATATYPE_EXPECT_UNION;
    }
    else if (S_EQ(str, "struct"))
    {
        type = DATATYPE_EXPECT_STRUCT;
    }
    return type;
}

char random_letter()
{
    return 'a' + rand() % 26;
}

struct token *random_name_generator()
{
    static const int NAME_LENGTH = 20;  
    char nome[NAME_LENGTH + 1];

    for (int i = 0; i < NAME_LENGTH; i++) {
        nome[i] = random_letter();
    }
    nome[NAME_LENGTH] = '\0';  

    char *sval = malloc(NAME_LENGTH + 1);
    if (!sval) {
        return NULL;  // handle malloc failure
    }
    strcpy(sval, nome);

    struct token *token = calloc(1, sizeof(struct token));
    if (!token) {
        free(sval);
        return NULL;
    }

    token->type = TOKEN_TYPE_IDENTIFIER;
    token->sval = sval;

    return token;
}

int parser_get_pointer_depth()
{ // LAB5
    int depth = 0;
    struct token *token = token_peek_next();
    while (token && token->type == TOKEN_TYPE_OPERATOR && S_EQ(token->sval, "*"))
    {
        depth++;
        token_next();
    }
    return depth;
}

bool parser_datatype_is_secondary_allowed_for_type(const char *type)
{
    return S_EQ(type, "long") || S_EQ(type, "short") || S_EQ(type, "double") || S_EQ(type, "float");
}

int parse_expressionable_single(struct history *history)
{
    struct token *token = token_peek_next();
    if (!token)
        return -1;

    history->flags |= NODE_FLAG_INSIDE_EXPRESSION;
    int res = -1;

    switch (token->type)
    {
    case TOKEN_TYPE_NUMBER:
        parse_single_token_to_node();
        res = 0;
        break;
    case TOKEN_TYPE_IDENTIFIER:
        parse_identifier(history);
        res = 0;
        break;
    case TOKEN_TYPE_OPERATOR:
        parse_exp(history);
        res = 0;
        break;
    case TOKEN_TYPE_KEYWORD:
        // Para keywords dentro de expressões, apenas avança
        token_next();
        res = 0;
        break;
    default:
        // Para outros tipos, apenas avança
        token_next();
        res = 0;
        break;
    }
    return res;
}

void parse_expressionable(struct history *history)
{
    int max_iterations = 100; // Limite para evitar loop infinito
    int iteration_count = 0;

    while (iteration_count < max_iterations && parse_expressionable_single(history) == 0)
    {
        iteration_count++;
    }

    if (iteration_count >= max_iterations)
    {
        printf("Aviso: Número máximo de iterações atingido em parse_expressionable\n");
    }
}

int parse_next()
{
    struct token *token = token_peek_next();
    if (!token)
        return -1;

    int res = 0;
    switch (token->type)
    {
    case TOKEN_TYPE_KEYWORD:
        if (S_EQ(token->sval, "if"))
        {
            token_next(); // Consome "if"
            parse_if(history_begin(0));
            res = 0;
        }
        else if (S_EQ(token->sval, "return"))
        {
            token_next(); // Consome "return"
            // Processa o valor de retorno
            parse_expressionable_root(history_begin(0));
            struct node *return_value = node_pop();
            if (return_value)
            {
                struct node return_node = {
                    .type = NODE_TYPE_STATEMENT_RETURN,
                    .pos = token->pos};
                struct node *created_return = node_create(&return_node);
                if (created_return)
                {
                    node_push(created_return);
                    printf("Return statement processado\n");
                }
            }
            // Verifica ponto e vírgula
            struct token *semicolon = token_next();
            if (!token_is_symbol(semicolon, ';'))
            {
                compiler_error(current_process, "Esperado ';' após return\n");
            }
            res = 0;
        }
        else if (keyword_is_datatype(token->sval))
        {
            parse_variable_function_or_struct_union(history_begin(0));
            res = 0;
        }
        else
        {
            // Para outros keywords, apenas avança
            token_next();
            res = 0;
        }
        break;
    case TOKEN_TYPE_NUMBER:
    case TOKEN_TYPE_IDENTIFIER:
    case TOKEN_TYPE_STRING:
        parse_expressionable(history_begin(0));
        res = 0;
        break;
    case TOKEN_TYPE_SYMBOL:
        // Ignora símbolos como chaves, parênteses, etc.
        token_next();
        res = 0;
        break;
    case TOKEN_TYPE_OPERATOR:
        // Processa operadores como parte de expressões
        parse_expressionable(history_begin(0));
        res = 0;
        break;
    default:
        // Avança o token se não conseguir processar
        token_next();
        res = 0;
        break;
    }
    return res;
}

// Para imprimir árvore e seus nós
void print_node_type(struct node *node)
{
    if (!node)
    {
        printf("NULL");
        return;
    }

    switch (node->type)
    {
    case NODE_TYPE_NUMBER:
        printf("NUMBER (%llu)", node->llnum);
        break;
    case NODE_TYPE_IDENTIFIER:
        printf("IDENTIFIER (%s)", node->sval ? node->sval : "null");
        break;
    case NODE_TYPE_STRING:
        printf("STRING (\"%s\")", node->sval ? node->sval : "null");
        break;
    case NODE_TYPE_EXPRESSION:
        printf("EXPRESSION (op: %s)", node->exp.op ? node->exp.op : "null");
        break;
    case NODE_TYPE_UNARY:
        printf("UNARY (op: %s)", node->exp.op ? node->exp.op : "null");
        break;
    case NODE_TYPE_EXPRESSION_PARENTHESES:
        printf("PAREN_EXPR");
        break;
    case NODE_TYPE_VARIABLE:
    {
        printf("VARIABLE (name: %s, type: %s",
               node->var.name ? node->var.name : "null",
               node->var.type.type_str ? node->var.type.type_str : "null");

        // Mostrar informações de array se aplicável
        if (node->var.type.flags & DATATYPE_FLAG_IS_ARRAY)
        {
            printf(", ARRAY[%zu", node->var.type.size);

            // Mostrar dimensões adicionais se houver
            struct datatype *current = node->var.type.datatype_secondary;
            while (current && current->size > 0)
            {
                printf("][%zu", current->size);
                current = current->datatype_secondary;
            }
            printf("]");
        }

        // Mostrar valor inicial se houver
        if (node->var.val)
        {
            printf(", initialized");
        }

        printf(")");
    }
    break;
    case NODE_TYPE_VARIABLE_LIST:
        printf("VARIABLE_LIST (count: %d)",
               node->var_list.list ? vector_count(node->var_list.list) : 0);
        break;
    case NODE_TYPE_STRUCT:
        printf("STRUCT (name: %s)", node->sval ? node->sval : "null");
        break;
    case NODE_TYPE_STATEMENT_IF:
        printf("IF_STATEMENT");
        break;
    case NODE_TYPE_STATEMENT_RETURN:
        printf("RETURN_STATEMENT");
        break;
    default:
        printf("UNKNOWN (type %d)", node->type);
        break;
    }
}

void print_tree(struct node *node, int level)
{
    if (node != NULL)
    {
        print_node_type(node);
        printf("  Level: %i\n", level);
        level++;

        switch (node->type)
        {
        case NODE_TYPE_EXPRESSION:
        case NODE_TYPE_UNARY:
        case NODE_TYPE_EXPRESSION_PARENTHESES:
            print_tree(node->exp.left, level);
            print_tree(node->exp.right, level);
            break;

        case NODE_TYPE_VARIABLE:
            if (node->var.val)
            {
                print_tree(node->var.val, level);
            }
            break;

        case NODE_TYPE_VARIABLE_LIST:
            if (node->var_list.list)
            {
                for (int i = 0; i < vector_count(node->var_list.list); i++)
                {
                    struct node **var_ptr = vector_at(node->var_list.list, i);
                    if (var_ptr && *var_ptr)
                    {
                        print_tree(*var_ptr, level);
                    }
                }
            }
            break;
        }
    }
}

int parse(struct compile_process *process)
{
    // Retorna caso tenha algum erro
    if (!process || !process->node_tree_vec || !process->token_vec || !process->node_vec)
    {
        return PARSE_GENERAL_ERROR;
    }
    current_process = process;
    parser_last_token = NULL;
    node_set_vector(process->node_vec, process->node_tree_vec);
    vector_set_peek_pointer(process->token_vec, 0);

    printf("\n\nArvore de nodes:\n");

    int max_iterations = 1000;
    int iteration_count = 0;

    // Processa cada token até o final
    while (iteration_count < max_iterations)
    {
        struct token *token = token_peek_next();
        if (!token)
            break;

        // Processa o próximo token
        if (parse_next() != 0)
            break;

        // Obtém o nó criado
        struct node *node = node_peek();
        if (!node)
        {
            printf("Warning: node_peek() returned NULL (iteração %d)\n", iteration_count);
            // Incrementa o contador mesmo quando node_peek retorna NULL
            iteration_count++;
            continue;
        }

        // Adiciona o nó à árvore
        vector_push(process->node_tree_vec, &node);

        // Imprime a árvore
        print_tree(node, 0);
        printf("\n\n");

        iteration_count++;
    }

    if (iteration_count >= max_iterations)
    {
        printf("Aviso: Número máximo de iterações atingido. Possível loop infinito detectado.\n");
    }

    return PARSE_ALL_OK;
}

void parser_datatype_adjust_size_for_secondary(struct datatype *datatype, struct token *datatype_secondary_token)
{
    if (!datatype_secondary_token)
        return;
    struct datatype *secondary_data_type = calloc(1, sizeof(struct datatype));
    parser_datatype_init_type_and_size_for_primitive(datatype_secondary_token, NULL, secondary_data_type);
    datatype->size += secondary_data_type->size;
    datatype->datatype_secondary = secondary_data_type;
    datatype->flags |= DATATYPE_FLAG_IS_SECONDARY;
}

void parser_datatype_init_type_and_size_for_primitive(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out)
{
    if (!parser_datatype_is_secondary_allowed_for_type(datatype_token->sval) && datatype_secondary_token)
    {
        compiler_error(current_process, "Voce utilizou um datatype secundario invalido!\n");
    }
    if (S_EQ(datatype_token->sval, "void"))
    {
        datatype_out->type = DATATYPE_VOID;
        datatype_out->size = 0;
    }
    else if (S_EQ(datatype_token->sval, "char"))
    {
        datatype_out->type = DATATYPE_CHAR;
        datatype_out->size = 1; // 1 BYTE
    }
    else if (S_EQ(datatype_token->sval, "short"))
    {
        datatype_out->type = DATATYPE_SHORT;
        datatype_out->size = 2; // 2 BYTES
    }
    else if (S_EQ(datatype_token->sval, "int"))
    {
        datatype_out->type = DATATYPE_INTEGER;
        datatype_out->size = 4; // 4 BYTES
    }
    else if (S_EQ(datatype_token->sval, "long"))
    {
        datatype_out->type = DATATYPE_LONG;
        datatype_out->size = 8; // 8 BYTES
    }
    else if (S_EQ(datatype_token->sval, "float"))
    {
        datatype_out->type = DATATYPE_FLOAT;
        datatype_out->size = 4; // 4 BYTES
    }
    else if (S_EQ(datatype_token->sval, "double"))
    {
        datatype_out->type = DATATYPE_DOUBLE;
        datatype_out->size = 8; // 8 BYTES
    }
    else
    {
        compiler_error(current_process, "BUG: Datatype invalido!\n");
    }
    parser_datatype_adjust_size_for_secondary(datatype_out, datatype_secondary_token);
}

void parser_datatype_init_type_and_size(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out, int pointer_depth, int expected_type)
{
    if (!(expected_type == DATATYPE_EXPECT_PRIMITIVE) && datatype_secondary_token)
    {
        compiler_error(current_process, "Voce utilizou um datatype secundario invalido!\n");
    }
    switch (expected_type)
    {
    case DATATYPE_EXPECT_PRIMITIVE:
        parser_datatype_init_type_and_size_for_primitive(datatype_token, datatype_secondary_token, datatype_out);
        break;
    case DATATYPE_EXPECT_STRUCT:
    case DATATYPE_EXPECT_UNION:
        compiler_error(current_process, "Struct e Unions ainda nao estao implementados!\n");
        break;
    default:
        compiler_error(current_process, "BUG: Erro desconhecido!\n");
    }
}

void parser_datatype_init(struct token *datatype_token, struct token *datatype_secondary_token, struct datatype *datatype_out, int pointer_depth, int expected_type)
{
    parser_datatype_init_type_and_size(datatype_token, datatype_secondary_token, datatype_out, pointer_depth, expected_type);
    datatype_out->type_str = datatype_token->sval;
}

void parse_datatype_type(struct datatype *dtype)
{ // LAB5
    struct token *datatype_token = NULL;
    struct token *datatype_secundary_token = NULL;
    parser_get_datatype_tokens(&datatype_token, &datatype_secundary_token);

    if (!datatype_token)
    {
        compiler_error(current_process, "Esperado tipo após modificadores\n");
        return;
    }

    int expected_type = parser_datatype_expected_for_type_string(datatype_token->sval);
    if (S_EQ(datatype_token->sval, "union") || S_EQ(datatype_token->sval, "struct"))
    {
        // Caso da struct com nome.
        struct token *next_token = token_peek_next();
        if (!next_token)
        {
            compiler_error(current_process, "Esperado identificador após struct/union\n");
            return;
        }
        if (next_token->type == TOKEN_TYPE_IDENTIFIER)
        {
            datatype_token = token_next();
        }
        else
        { 
            datatype_token = random_name_generator();
            if (!datatype_token)
            {
                compiler_error(current_process, "Falha ao gerar nome para tipo anônimo\n");
                return;
            }
            dtype->flags |= DATATYPE_FLAG_IS_STRUCT_UNION_NO_NAME;
        }
    }
    // Descobre a quantidade de ponteiros.
    int pointer_depth = parser_get_pointer_depth();
    parser_datatype_init(datatype_token, datatype_secundary_token, dtype, pointer_depth, expected_type);
}

void parse_datatype_modifiers(struct datatype *dtype)
{ // LAB5
    struct token *token = token_peek_next();
    if (!token)
        return;

    while (token && token->type == TOKEN_TYPE_KEYWORD)
    {
        if (!is_keyword_variable_modifier(token->sval))
            break;
        if (S_EQ(token->sval, "signed"))
        {
            dtype->flags |= DATATYPE_FLAG_IS_SIGNED;
        }
        else if (S_EQ(token->sval, "unsigned"))
        {
            dtype->flags &= ~DATATYPE_FLAG_IS_SIGNED;
        }
        else if (S_EQ(token->sval, "static"))
        {
            dtype->flags |= DATATYPE_FLAG_IS_STATIC;
        }
        else if (S_EQ(token->sval, "const"))
        {
            dtype->flags |= DATATYPE_FLAG_IS_CONST;
        }
        else if (S_EQ(token->sval, "extern"))
        {
            dtype->flags |= DATATYPE_FLAG_IS_EXTERN;
        }
        else if (S_EQ(token->sval, "__ignore_typecheck__"))
        {
            dtype->flags |= DATATYPE_FLAG_IS_IGNORE_TYPE_CHECKING;
        }
        token_next();
        token = token_peek_next();
    }
}

void parse_datatype(struct datatype *dtype)
{
    memset(dtype, 0, sizeof(struct datatype));
    dtype->flags |= DATATYPE_FLAG_IS_SIGNED;
    parse_datatype_modifiers(dtype);
    parse_datatype_type(dtype);
    parse_datatype_modifiers(dtype);
}

void parse_variable_function_or_struct_union(struct history *history)
{
    struct datatype dtype;
    parse_datatype(&dtype);
    struct token *name_token = token_next();
    if (name_token->type != TOKEN_TYPE_IDENTIFIER)
    {
        compiler_error(current_process, "Variavel declarada sem nome!\n");
        return;
    }
    parse_variable(&dtype, name_token, history);
}

void parse_keyword(struct history *history)
{ // LAB 5
    struct token *token = token_peek_next();
    if (!token)
        return;

    if (is_keyword_variable_modifier(token->sval) || keyword_is_datatype(token->sval))
    {
        // Cria um novo node para a declaração
        struct node *node = node_create(&(struct node){
            .type = NODE_TYPE_VARIABLE,
            .pos = token->pos});

        if (!node)
        {
            compiler_error(current_process, "Falha ao criar node para declaração\n");
            return;
        }

        // Parse o datatype
        struct datatype dtype;
        parse_datatype(&dtype);

        // Armazena o datatype no node
        node->any = malloc(sizeof(struct datatype));
        if (!node->any)
        {
            compiler_error(current_process, "Falha ao alocar memória para datatype\n");
            return;
        }
        memcpy(node->any, &dtype, sizeof(struct datatype));

        // Adiciona o node à pilha
        node_push(node);
        return;
    }

    compiler_error(current_process, "Keyword não implementada: %s\n", token->sval);
}

static bool token_next_is_operator(const char *op)
{
    struct token *token = token_peek_next();
    return token_is_operator(token, op);
}

void parse_expressionable_root(struct history *history)
{
    parse_expressionable(history);
    struct node *result_node = node_pop();
    if (result_node)
    {
        node_push(result_node);
    }
}

void append_array_dimension(struct datatype *base, int size)
{
    while (base->datatype_secondary)
    {
        base = base->datatype_secondary;
    }
    base->datatype_secondary = calloc(1, sizeof(struct datatype));
    base->datatype_secondary->size = size;
}

void parse_variable(struct datatype *dtype, struct token *name_token, struct history *history)
{
    // Criar lista de variáveis usando a estrutura varlist
    struct node var_list_node = {.type = NODE_TYPE_VARIABLE_LIST, .var_list.list = vector_create(sizeof(struct node *))};

    if (!var_list_node.var_list.list)
    {
        compiler_error(current_process, "Falha ao criar lista de variáveis\n");
        return;
    }

    struct node *value_node = NULL;
    int array_dims[8] = {0};
    int array_dim_count = 0;

    while (token_next_is_operator("["))
    {
        token_next();
        struct token *size_token = token_next();
        if (size_token->type != TOKEN_TYPE_NUMBER)
        {
            compiler_error(current_process, "Esperado número como tamanho do array\n");
            break;
        }
        array_dims[array_dim_count++] = size_token->inum;
        struct token *close_bracket = token_next();
    }

    if (token_next_is_operator("="))
    {
        token_next();
        parse_expressionable_root(history);
        value_node = node_pop();
    }

    make_variable_node(dtype, name_token, value_node);
    struct node *var_node = node_pop();
    if (!var_node)
    {
        compiler_error(current_process, "Falha ao criar node para variável\n");
        vector_free(var_list_node.var_list.list);
        return;
    }

    if (array_dim_count > 0) {
    var_node->var.type.flags |= DATATYPE_FLAG_IS_ARRAY;
    var_node->var.type.pointer_depth = array_dim_count;
    var_node->var.type.size = array_dims[0];
    for (int i = 1; i < array_dim_count; i++) {
        append_array_dimension(&var_node->var.type, array_dims[i]);
    }
}


    vector_push(var_list_node.var_list.list, &var_node);

    while (token_next_is_operator(","))
    {
        token_next();
        struct token *next_name_token = token_next();
        if (next_name_token->type != TOKEN_TYPE_IDENTIFIER)
        {
            compiler_error(current_process, "Esperado identificador após vírgula\n");
            break;
        }

        array_dim_count = 0;
        while (token_next_is_operator("["))
        {
            token_next();
            struct token *size_token = token_next();

            if (size_token->type != TOKEN_TYPE_NUMBER)
            {
                compiler_error(current_process, "Esperado número como tamanho do array\n");
                break;
            }

            array_dims[array_dim_count++] = size_token->inum;
            struct token *close_bracket = token_next();
            if (!token_is_operator(close_bracket, "]"))
            {
                compiler_error(current_process, "Esperado ']' após tamanho do array\n");
                break;
            }
        }
        value_node = NULL;

        if (token_next_is_operator("="))
        {
            token_next();
            parse_expressionable_root(history);
            value_node = node_pop();
        }

        make_variable_node(dtype, next_name_token, value_node);
        var_node = node_pop();

        if (!var_node)
        {
            compiler_error(current_process, "Falha ao criar node para variável\n");
            break;
        }

        if (array_dim_count > 0)
        {
            var_node->var.type.flags |= DATATYPE_FLAG_IS_ARRAY;
            var_node->var.type.pointer_depth = array_dim_count;
            for (int i = 0; i < array_dim_count; i++)
            {

                if (i == 0)
                {
                    var_node->var.type.size = array_dims[0];
                }
                else
                {

                    if (!var_node->var.type.datatype_secondary)
                    {
                        var_node->var.type.datatype_secondary = calloc(1, sizeof(struct datatype));
                    }

                    var_node->var.type.datatype_secondary->size = array_dims[i];
                }
            }
        }
        vector_push(var_list_node.var_list.list, &var_node);
    }

    struct token *semicolon = token_next();
    if (!token_is_symbol(semicolon, ';'))
    {
        compiler_error(current_process, "Esperado ';' após declaração de variáveis\n");
        vector_free(var_list_node.var_list.list);
        return;
    }

    struct node *created_node = node_create(&var_list_node);
    if (!created_node)
    {
        compiler_error(current_process, "Falha ao criar node da lista de variáveis\n");
        vector_free(var_list_node.var_list.list);
        return;
    }

    for (int i = 0; i < vector_count(var_list_node.var_list.list); i++)
    {
        struct node **var_ptr = vector_at(var_list_node.var_list.list, i);
        if (!var_ptr || !*var_ptr)
            continue;
        struct token temp_token = {.type = TOKEN_TYPE_IDENTIFIER, .sval = (*var_ptr)->var.name, .pos = (*var_ptr)->pos};
        make_variable_node_and_register(history, dtype, &temp_token, (*var_ptr)->var.val);
    }
    node_push(created_node);
}

void make_variable_node_and_register(struct history *history, struct datatype *dtype, struct token *name_token, struct node *value_node)
{
    make_variable_node(dtype, name_token, value_node);
    struct node *var_node = node_pop();
    node_push(var_node);
}

void make_variable_node(struct datatype *dtype, struct token *name_token, struct node *value_node)
{
    const char *name_str = NULL;
    if (name_token) {
        name_str = name_token->sval;
    }
    node_create(&(struct node){.type = NODE_TYPE_VARIABLE, .var.name = name_str, .var.type = *dtype, .var.val = value_node});
}

static bool token_is_parenthesis(struct token *token, char paren)
{
    if (!token)
        return false;

    if (token->type == TOKEN_TYPE_OPERATOR && S_EQ(token->sval, "("))
    {
        return true;
    }

    if (token->type == TOKEN_TYPE_SYMBOL && token->cval == paren)
    {
        return true;
    }

    return false;
}

void parse_if(struct history *history)
{
    struct token *next_token = token_peek_next();
    if (!next_token)
    {
        compiler_error(current_process, "Erro no if");
        return;
    }

    if (!token_is_parenthesis(next_token, '('))
    {
        token_next();
        next_token = token_peek_next();
        if (!next_token || !token_is_parenthesis(next_token, '('))
        {
            compiler_error(current_process, "If tem que ter parentese\n");
            return;
        }
    }

    token_next();

    parse_expressionable_root(history);
    struct node *condition = node_pop();
    if (!condition)
    {
        printf("Erro ao Processar if\n");
    }
    else
    {
        printf("If processado\n");
    }

    struct token *close_paren = token_next();
    if (!close_paren || !token_is_parenthesis(close_paren, ')'))
    {
        compiler_error(current_process, "If tem que ter parenteses\n");
        return;
    }

    // Verifica chave de abertura
    struct token *open_brace = token_next();
    if (!open_brace || !token_is_symbol(open_brace, '{'))
    {
        compiler_error(current_process, "If tem que ter chaves\n");
        return;
    }

    // Processa o corpo do if
    struct vector *body_statements = vector_create(sizeof(struct node *));
    if (!body_statements)
    {
        compiler_error(current_process, "Erro ao adicionar os argumentos do if\n");
        return;
    }

    while (true)
    {
        struct token *next_token = token_peek_next();
        if (!next_token)
            break;

        if (token_is_symbol(next_token, '}'))
        {
            token_next();
            break;
        }

        // Processa statement
        if (parse_next() == 0)
        {
            struct node *statement = node_pop();
            if (statement)
            {
                vector_push(body_statements, &statement);
                printf("Argumento do if adicionadof\n");
            }
        }
        else
        {
            token_next();
        }
    }

    struct node if_node = {.type = NODE_TYPE_STATEMENT_IF, .pos = next_token ? next_token->pos : (struct pos){0, 0, NULL}};

    struct node *created_if = node_create(&if_node);
    if (!created_if)
    {
        compiler_error(current_process, "Falha ao criar node do if\n");
        vector_free(body_statements);
        return;
    }

    printf("O if possui %d argumentos\n", vector_count(body_statements));
    node_push(created_if);
}