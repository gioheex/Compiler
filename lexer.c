#include "compiler.h"
#include <string.h>
#include "helpers/vector.h"
#include "helpers/buffer.h"
#include <assert.h>

#define LEX_GETC_IF(buffer, c, exp)     \
    for (c = peekc(); exp; c = peekc()) \
    {                                   \
        buffer_write(buffer, c);        \
        nextc();                        \
    }

static struct token tmp_token;
static struct lex_process *lex_process;

// Pega um caractere do arquivo, sem mudar a posição de leitura
static char peekc()
{
    return lex_process->function->peek_char(lex_process);
}

// Pega o próximo caractere e avança a posição de leitura
static char nextc()
{
    char c = lex_process->function->next_char(lex_process);
    if (c == '\n')
    {
        lex_process->pos.line += 1;
        lex_process->pos.col = 1;
    }
    else
    {
        lex_process->pos.col += 1;
    }
    return c;
}

// Adiciona um caractere no arquivo
static void pushc(char c)
{
    lex_process->function->push_char(lex_process, c);
}

static struct pos lex_file_position()
{
    return lex_process->pos;
}

static struct token *token_create(struct token *_token)
{
    memcpy(&tmp_token, _token, sizeof(struct token));
    tmp_token.pos = lex_file_position();
    return &tmp_token;
}

static struct token *lexer_last_token()
{
    return vector_back_or_null(lex_process->token_vec);
}

static struct token *read_next_token();

static struct token *handle_whitespace()
{
    struct token *last_token = lexer_last_token();
    if (last_token)
    {
        last_token->whitespace = true;
    }
    nextc();
    return read_next_token();
}

const char *read_number_str()
{
    const char *num = NULL;
    struct buffer *buffer = buffer_create();
    char c = peekc();
    LEX_GETC_IF(buffer, c, (c >= '0' && c <= '9'));
    // Finaliza a string
    buffer_write(buffer, 0x00);

    printf("Token: %s\n", buffer->data);

    return buffer_ptr(buffer);
}

unsigned long long read_number()
{
    const char *s = read_number_str();
    return atoll(s);
}

struct token *token_make_number_for_value(unsigned long number)
{
    return token_create(&(struct token){.type = TOKEN_TYPE_NUMBER, .llnum = number});
}

struct token *token_make_number()
{
    return token_make_number_for_value(read_number());
}

struct token *token_make_string(char inicio, char fim)
{
    struct buffer *buf = buffer_create();
    assert(nextc() == inicio);
    char c = nextc();

    for (; c != inicio && c != EOF; c = nextc())
    {
        if (c == '\\')
        {
            continue;
        }
        buffer_write(buf, c);
    }

    buffer_write(buf, 0x00);
    printf("Token: %s\n", buf->data);
    return token_create(&(struct token){.type = TOKEN_TYPE_STRING, .sval = buffer_ptr(buf)});
}

struct token* single_line_comment() {

    struct buffer* buf = buffer_create();
    char c = "";

    LEX_GETC_IF(buf, c, c != '\n' && c != EOF);
    buffer_write(buf, 0x00);

    printf("Token: %s\n", buf->data);

    return token_create(&(struct token){.type = TOKEN_TYPE_COMMENT, .sval = buffer_ptr(buf)});
}

struct token* multi_line_comment(char estrela) {

    struct buffer* buf = buffer_create();
    char c = "";

    for (; c != estrela && c != EOF; c = nextc())
    {
        if (c == estrela) {
            nextc(); 

            if (peekc() == '/') {
                nextc();
                buffer_write(buf, 0x00);
                printf("Token: %s\n", buf->data);
                return token_create(&(struct token){.type = TOKEN_TYPE_STRING, .sval = buffer_ptr(buf)});
            }
        }
        buffer_write(buf, c);
    }

}

struct token *token_make_comment() {

    char c = peekc();

    if (c == '/') {
        nextc();
        if (peekc() == '/') {
            nextc();
            return single_line_comment();
        } else if (peekc() == '*') {
            nextc();
            return multi_line_comment('*');
        }
    }
}

struct token *token_make_operator()
{
    struct buffer *bufa = buffer_create();
    char c = peekc();
    LEX_GETC_IF(bufa, c, (c >= '0' && c <= '9'));
    // Finaliza a string
    buffer_write(bufa, 0x00);

    printf("Token: %s\n", bufa->data);

    return buffer_ptr(bufa);
}

struct token *read_next_token()
{
    struct token *token = NULL;
    char c = peekc();
    switch (c)
    {
    case EOF:
        // Fim do arquivo
        break;

    NUMERIC_CASE:
        token = token_make_number();
        break;

    case '"':
        token = token_make_string('"', '"');
        break;

    OPERATOR_CASE:
        token = token_make_operator();
        break;

    case '/':
        token = token_make_comment();
        break;

    case ' ':
    case '\t':
    case '\n':
        token = handle_whitespace();
        break;

    default:
        // compiler_error(lex_process->compiler, "Token inválido\n");
        break;
    }

    return token;
}

int lex(struct lex_process *process)
{
    lex_process = process;
    process->current_expression_count = 0;
    process->parentheses_buffer = NULL;
    process->pos.filename = process->compiler->cfile.abs_path;
    process->pos.line = 1;
    process->pos.col = 1;

    struct token *token = read_next_token();
    // Ler todos os tokens do arquivo de input
    while (token)
    {
        vector_push(process->token_vec, token);
        token = read_next_token();
    }

    return LEXICAL_ANALYSIS_ALL_OK;
}