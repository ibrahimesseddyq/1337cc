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
        const char *sval;
        unsigned int inum;
        unsigned long lnum;
        unsigned long long llnum;
        void* any;
    };
    // True if their whitespace is between the token and the next one
    bool whitespace;
};


enum 
{
    COMPILER_FILE_COMPLETED_OK,
    COMPILER
}
 #endif