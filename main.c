#include <stdio.h>
#include "helpers/vector.h"
#include "helpers/buffer.h"
#include "compiler.h"

int main(){
    printf("Compiladores - TURMA A - GRUPO 1\n");

    struct vector *file_vector = vector_create(sizeof(char*));
    
    char *file1 = "source1.c";
    char *file2 = "cprocess.c";
    vector_push(file_vector, &file1);
    vector_push(file_vector, &file2);
    
    vector_set_peek_pointer(file_vector, file_vector->rindex - 1);
    
    while (!vector_empty(file_vector)) {
        char **current_file = vector_peek(file_vector);
        if (current_file) {
            printf("Compilando o arquivo: %s\n", *current_file);
            int result = compile_file(*current_file, "output.o", 0);
            if (result == COMPILER_FILE_COMPILED_OK) {
                printf("Compilação de %s bem-sucedida.\n", *current_file);
            } else {
                printf("Erro na compilação de %s.\n", *current_file);
            }
        }

        vector_pop(file_vector);
    }
    
    vector_free(file_vector);

    return 0;
}