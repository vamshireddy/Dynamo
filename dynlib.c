/*
 * Dynamic library loader and unloader for high performance server.
 *
 * Author: Vamshi Redy Konagari
 * Email: vkonagar@andrew.cmu.edu
 * Date: 2/19/2017
 */
#include "dynlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

void* load_dyn_library(char* library_name)
{
    void* handle;
    void (*execute)(int fd, char* args[], int count);
    char* error;

    /* Dynamically load the library */
    handle = dlopen(library_name, RTLD_LAZY);
    if (!handle)
    {
        fprintf(stderr, "%s\n", dlerror());
        return NULL;
    }
    return handle;
}

int unload_dyn_library(void* handle)
{
    if (dlclose(handle) < 0)
    {
        fprintf(stderr, "%s\n", dlerror());
        return -1;
    }
}
