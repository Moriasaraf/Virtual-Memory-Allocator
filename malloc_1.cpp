#include <unistd.h>



void* smalloc(size_t size){
    if (size == 0 || size > 100000000) return NULL;

    void* temp = sbrk(size);
    if (temp == (void *) -1) return NULL;
    return temp;
}