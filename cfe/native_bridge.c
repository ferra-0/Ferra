#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#include <stddef.h>
#include <stdlib.h>

size_t strlen(const char* value);
void* memcpy(void* destination, const void* source, size_t size);

/* LLVM IR calls the portable spelling directly; UCRT exports _strdup. */
char* strdup(const char* value) {
    if (value == NULL) return NULL;
    const size_t size = strlen(value) + 1;
    char* copy = (char*)malloc(size);
    if (copy != NULL) memcpy(copy, value, size);
    return copy;
}
#endif

/* Keep UCRT's stdin accessor detail out of generated LLVM IR. */
void* ferra_stdin(void) {
    return (void*)stdin;
}

int efe_native_call_bridge(
    void* callback,
    void* receiver,
    void* args,
    int argc,
    void* out
) {
    int (*fn)(void*, void*, int, void*) = (int (*)(void*, void*, int, void*))callback;
    return fn(receiver, args, argc, out);
}
