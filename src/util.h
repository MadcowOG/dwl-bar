#ifndef UTIL_H_
#define UTIL_H_
#include <sys/types.h>
#include <stddef.h>

typedef struct {
    void* ptr;
    uint size;
} Stack;

#define stack_for_each(pos, stack) \
    for (pos = (stack)->data); \
    (const char*) pos < ((const char*) (stack)->data + (stack)->size); \
    (pos)++)


inline void stack_init(Stack* stack) {
    stack->ptr = NULL;
    stack->size = 0;
}

void* stack_end(Stack* stack);
void* stack_push(Stack* stack);

#endif // UTIL_H_
