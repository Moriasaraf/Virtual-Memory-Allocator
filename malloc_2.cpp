#include <unistd.h>
#include <cstring>

struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};

MallocMetadata* malloc_list;
int malloc_list_size = 0;
MallocMetadata* malloc_list_last;

void* smalloc(size_t size){
    if (size == 0 || size > 100000000) return NULL;

    //check if there is an empty block.
    if (malloc_list_size != 0){
        MallocMetadata* temp = malloc_list;
        for (int i = 0; i < malloc_list_size; i++){
            if (temp->is_free == true && size <= temp->size){

                //reset metadata.
                temp->is_free = false;
                return temp + 1;
            }
            temp = temp->next;
        }
    }

    //otherwise, use sbrk to make more space.
    MallocMetadata* new_space = (MallocMetadata*)sbrk(size + sizeof(MallocMetadata));
    if (new_space == (void*)-1) return NULL;

    new_space->size = size;
    new_space->next = NULL;
    new_space->is_free = false;

    if (malloc_list_size == 0){
        new_space->prev = NULL;
        malloc_list_last = new_space;
        malloc_list = new_space;
    }else{
        new_space->prev = malloc_list_last;
        malloc_list_last->next = new_space;
        malloc_list_last = new_space;
    }

    malloc_list_size++;

    return new_space + 1;
}



void* scalloc(size_t num, size_t size){
    if (size == 0 || num == 0) return NULL;

    void* space = smalloc(num*size);
    if (space == NULL) return NULL;

    std::memset(space, 0, num*size);

    return space;
}


void sfree(void* p){
    if (p == NULL) return;
    MallocMetadata* space_meta = (MallocMetadata*)p - 1;
    space_meta->is_free = true;
    return;
}


void* srealloc(void* oldp, size_t size){
    if (size == 0 || size > 100000000) return NULL;
    if (oldp == NULL) return smalloc(size);

    MallocMetadata* old_space_meta = (MallocMetadata*)oldp - 1;
    if (size <= old_space_meta->size) return oldp;

    void* new_space = smalloc(size);
    if (new_space == NULL) return NULL;

    std::memmove(new_space, oldp, old_space_meta->size);
    sfree(oldp);

    return new_space;
}

size_t _num_free_blocks(){
    size_t count = 0;
    MallocMetadata* temp = malloc_list;
    for (int i = 0; i < malloc_list_size; i++){
        if (temp->is_free == true) count ++;
        temp = temp->next;
    }
    return count;
}

size_t _num_free_bytes(){
    size_t count = 0;
    MallocMetadata* temp = malloc_list;
    for (int i = 0; i < malloc_list_size; i++){
        if (temp->is_free == true) count += temp->size;
        temp = temp->next;
    }
    return count;
}

size_t _num_allocated_blocks(){
    return malloc_list_size;
}

size_t _num_allocated_bytes(){
    if (malloc_list_size == 0) return 0;

    size_t count = 0;
    MallocMetadata* temp = malloc_list;
    for (int i = 0; i < malloc_list_size; i++){
        count += temp->size;
        temp = temp->next;
    }
    return count;
}

size_t _num_meta_data_bytes(){
    return malloc_list_size * sizeof(MallocMetadata);
}

size_t _size_meta_data(){
    return sizeof(MallocMetadata);
}