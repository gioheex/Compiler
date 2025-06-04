#include <stdio.h>

int x = 5;

int main () {
    int x = 10;
    {
        int x = 20;
        printf("%d\n", x);
    }
    printf("%d\n", x);
}
