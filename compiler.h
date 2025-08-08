 #ifndef COMPILER_H
 #define COMPILER_H 

#include <stdio.h>

struct token 
{
    int type;
    int flags;

    union 
    {
        char cval;
        const char *sval 
    }
}

 #endif