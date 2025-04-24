#include "compiler.h"
#include <string.h>
#include "helpers/vector.h"
#include "helpers/buffer.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>

#define LEX_GETC_IF(buffer, c, exp)     \
    for (c = peekc(); exp; c = peekc()) \
    {                                   \
        buffer_write(buffer, c);        \
        nextc();                        \
    }

#define S_EQ(a, b) (strcmp((a), (b)) == 0)

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

bool is_keyword(const char *str)
{
    return S_EQ(str, "unsigned") ||
           S_EQ(str, "signed") ||
           S_EQ(str, "char") ||
           S_EQ(str, "short") ||
           S_EQ(str, "int") ||
           S_EQ(str, "long") ||
           S_EQ(str, "float") ||
           S_EQ(str, "double") ||
           S_EQ(str, "void") ||
           S_EQ(str, "struct") ||
           S_EQ(str, "union") ||
           S_EQ(str, "static") ||
           S_EQ(str, "__ignore_typecheck") ||
           S_EQ(str, "return") ||
           S_EQ(str, "include") ||
           S_EQ(str, "sizeof") ||
           S_EQ(str, "if") ||
           S_EQ(str, "else") ||
           S_EQ(str, "while") ||
           S_EQ(str, "for") ||
           S_EQ(str, "do") ||
           S_EQ(str, "break") ||
           S_EQ(str, "continue") ||
           S_EQ(str, "switch") ||
           S_EQ(str, "case") ||
           S_EQ(str, "default") ||
           S_EQ(str, "goto") ||
           S_EQ(str, "typedef") ||
           S_EQ(str, "const") ||
           S_EQ(str, "extern") ||
           S_EQ(str, "retrict");
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

void lex_new_expression()
{
    lex_process->current_expression_count++;
    if (lex_process->current_expression_count == 1)
    {
        lex_process->parentheses_buffer = buffer_create();
    }
}

void lex_fisish_expression()
{
    lex_process->current_expression_count--;
    if (lex_process->current_expression_count < 0)
    {
        compiler_error(lex_process->compiler, "Voce fechou uma expressao nunca iniciada!\n");
    }
}

bool lex_is_in_expression()
{
    return lex_process->current_expression_count > 0;
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

    if (c == '0')
    {
        char prox = nextc();
        prox = nextc();
        struct buffer *buff = buffer_create();
        char *stopstring;
        long l;

        if (prox == 'x') {
            LEX_GETC_IF(buff, prox, prox != ';');
            l = strtol(buff->data, &stopstring, 16);
            printf("%ld\n\n\n", l);
        }

        else if (prox == 'b') {
            return 0;
        }

        else {
            pushc(prox);
            pushc('0');
            LEX_GETC_IF(buffer, c, (c >= '0' && c <= '9'));
            // Finaliza a string
            buffer_write(buffer, 0x00);

            printf("Token: %s\n", buffer->data);

            return buffer_ptr(buffer);
        }
    }
    else {
        LEX_GETC_IF(buffer, c, (c >= '0' && c <= '9'));
        // Finaliza a string
        buffer_write(buffer, 0x00);

        printf("Token: %s\n", buffer->data);

        return buffer_ptr(buffer);
    }
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

    for (; c != fim && c != EOF; c = nextc())
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

static struct token *token_make_symbol()
{
    char c = nextc();
    if (c == ')')
    {
        lex_fisish_expression();
    }

    struct token *token = token_create(&(struct token){.type = TOKEN_TYPE_SYMBOL, .cval = c});
    printf("Token: %c\n", c);
    return token;
}

char *read_op()
{
    struct buffer *buf = buffer_create();
    char c = nextc();

    switch (c)
    {
    OPERATOR_CASE:
        buffer_write(buf, c);
        break;
    default:
        return NULL; // não é operador
    }

    char next = peekc();

    // Verifica operadores compostos (dois caracteres)
    if ((c == '=' && next == '=') ||
        (c == '+' && next == '=') ||
        (c == '-' && next == '=') ||
        (c == '*' && next == '=') ||
        (c == '/' && next == '=') ||
        (c == '+' && next == '+') ||
        (c == '-' && next == '-') ||
        (c == '!' && next == '=') ||
        (c == '<' && next == '=') ||
        (c == '>' && next == '=') ||
        (c == '&' && next == '&') ||
        (c == '|' && next == '|'))
    {

        buffer_write(buf, nextc()); // Consome o próximo caractere
    }

    buffer_write(buf, 0x00);
    return buf->data;
}

bool token_is_keyword(struct token *token, char *kw)
{
    return S_EQ(token->sval, kw);
}

static struct token *token_make_operator_or_string()
{
    char op = peekc(); // Olha o próximo caractere

    if (op == '<')
    {
        struct token *last_token = lexer_last_token();
        if (token_is_keyword(last_token, "include"))
        {
            return token_make_string('<', '>');
        }
    }

    struct token *token = token_create(
        &(struct token){.type = TOKEN_TYPE_STRING, .sval = read_op()});

    printf("Token: %s\n", token->sval);

    if (op == '(')
    {
        lex_new_expression();
    }

    return token;
}

struct token *single_line_comment()
{

    struct buffer *buf = buffer_create();
    char c = 0;

    LEX_GETC_IF(buf, c, c != '\n' && c != EOF);
    buffer_write(buf, 0x00);

    printf("Token: %s\n", buf->data);

    return token_create(&(struct token){.type = TOKEN_TYPE_COMMENT, .sval = buffer_ptr(buf)});
}

struct token *multiline_comment(char estrela)
{
    struct buffer *buf = buffer_create();
    if (!buf)
        return NULL; // checa se buffer foi criado

    char c = nextc();

    while (c != EOF)
    {
        if (c == estrela)
        {
            char next = peekc();
            if (next == '/')
            {
                nextc();
                buffer_write(buf, 0x00);
                printf("Token: %s\n", buf->data);
                return token_create(&(struct token){.type = TOKEN_TYPE_STRING, .sval = buffer_ptr(buf)});
            }
        }

        buffer_write(buf, c);
        c = nextc();
    }

    return NULL;
}

struct token *token_make_comment()
{

    char c = peekc();

    if (c == '/')
    {
        nextc();
        if (peekc() == '/')
        {
            nextc();
            return single_line_comment();
        }
        else if (peekc() == '*')
        {
            nextc();
            return multiline_comment('*');
        }

        pushc('/');
        return token_make_operator_or_string();
    }
}

struct token *token_make_newline()
{
    nextc();
    return token_create(&(struct token){
        .type = TOKEN_TYPE_NEWLINE,
        .sval = "\\n"});
}

static struct token *make_identifier_or_keyword()
{
    struct buffer *buffer = buffer_create();
    char c;
    LEX_GETC_IF(buffer, c, (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')

    buffer_write(buffer, 0x00);
    printf("Token: %s\n", buffer->data);

    if (is_keyword(buffer_ptr(buffer)))
    {
        return token_create(&(struct token){.type = TOKEN_TYPE_KEYWORD, .sval = buffer_ptr(buffer)});
    }

    return token_create(&(struct token){.type = TOKEN_TYPE_IDENTIFIER, .sval = buffer_ptr(buffer)});
}

struct token *read_special_token()
{
    char c = peekc();
    if (isalpha(c) || c == '_')
    {
        return make_identifier_or_keyword();
    }

    return NULL;
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

    case '/':
        token = token_make_comment();
        break;

    case '\n':
        token = token_make_newline();
        break;

    NUMERIC_CASE:
        token = token_make_number();
        break;

    OPERATOR_CASE:
        token = token_make_operator_or_string();
        break;

    SYMBOL_CASE:
        token = token_make_symbol();
        break;

    case '"':
        token = token_make_string('"', '"');
        break;

    case ' ':
        token = handle_whitespace();
        break;

    case '\t':

    default:
        token = read_special_token();
        if (!token)
        {
            compiler_error(lex_process->compiler, "Token invalido\n");
        }
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