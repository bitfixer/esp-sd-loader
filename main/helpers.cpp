#include "helpers.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

void upperStringInPlace(char* str)
{
    char* c = str;
    while (*c != 0)
    {
        *c = toupper(*c);
        c++;
    }
}