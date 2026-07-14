#include <unistd.h>
#include <cstring>
#include <sys/mman.h>

#define MAX_ORDER 10
#define KB 1024
size_t initial_blocks_amount = 32;
const size_t alignment = initial_blocks_amount * 128 * KB;

size_t total_allocated_blocks = 0;
size_t total_allocated_bytes = 0;


struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
    int order;
};

MallocMetadata* memory_order_list[MAX_ORDER + 1] = {NULL};
bool is_initialized = false;

MallocMetadata* mapped_memory = NULL;


void split_block(MallocMetadata* block){
    int old_order = block->order;
    int new_order = old_order -1;

    size_t total_blocks_size = 128 * (1 << old_order); //aka 2^old_order
    size_t new_block_size = total_blocks_size / 2;


    MallocMetadata* second_block = (MallocMetadata*)((char*)block + new_block_size);

    block->size = new_block_size - sizeof(MallocMetadata);
    second_block->size = new_block_size - sizeof(MallocMetadata);
    block->order = new_order;
    second_block->order = new_order;
    second_block->is_free = true;


    //detach block
    if (block->prev != NULL){
        block->prev->next = block->next;
    }else{
        memory_order_list[old_order] = block->next;
    }

    if (block->next != NULL){
        block->next->prev = block->prev;
    }

    //connect buddies.
    block->next = second_block;
    second_block->prev = block;

    block->prev = NULL;
    second_block->next = NULL;

    //add to list
    if (memory_order_list[new_order] == NULL){
        memory_order_list[new_order] = block;
    }else if (block < memory_order_list[new_order]){
        //should be first
        second_block->next = memory_order_list[new_order];
        memory_order_list[new_order]->prev = second_block;
        memory_order_list[new_order] = block;
    }else{

        MallocMetadata* temp = memory_order_list[new_order];
        while(temp->next != NULL && temp->next < block){
            temp = temp->next;
        }

        second_block->next = temp->next;

        if (temp->next != NULL){
            temp->next->prev = second_block;
        }
        block->prev = temp;
        temp->next = block;
    }

    total_allocated_blocks++;
}

size_t _size_meta_data();

void* smalloc(size_t size){
    if (size == 0 || size > 100000000) return NULL;
    
    size_t total_block_size = size + _size_meta_data();
    if (size + sizeof(MallocMetadata) > 128 * KB) {
        void* mem = mmap(NULL, total_block_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) return NULL;
        
        MallocMetadata* meta = (MallocMetadata*)mem;
        meta->size = size;
        meta->is_free = false;
        meta->next = NULL;
        meta->prev = NULL;

        if (mapped_memory == NULL){
            mapped_memory = meta;
        }else{
            meta->next = mapped_memory;
            mapped_memory->prev = meta;
            mapped_memory = meta;
        }
        
        return (void*)((char*)mem + _size_meta_data());
    }



    //initialise if it is the first time.
    if (is_initialized == false){
        //create the initial_blocks_amount blocks.
        void* currect_break = sbrk(0);
        size_t miss_allingment = (intptr_t)currect_break % alignment;
        total_allocated_blocks = initial_blocks_amount;

        if (miss_allingment != 0){
            if (sbrk(alignment - miss_allingment) == (void*)-1) return NULL;
        }

        void* allocated = sbrk(initial_blocks_amount*128*KB);
        if (allocated == (void*)-1) return NULL;
        //allocated is the start of the memory allocated (alligned)

        //split the space and create blocks
        MallocMetadata* prev = NULL;
        for (int i = 0; i < initial_blocks_amount; i++){
            void* curr_block_addr = (char*)allocated + (i * 128*KB);
            MallocMetadata* meta = (MallocMetadata*)curr_block_addr;
            meta->size = 128*KB - sizeof(MallocMetadata);
            meta->is_free = true;
            meta->order = MAX_ORDER;

            meta->prev = prev;
            meta->next = NULL;
        
            if (prev != NULL) {
                prev->next = meta;
            }else{
                memory_order_list[MAX_ORDER] = meta;
            }
        
            prev = meta;
        }


        is_initialized = true;
    }


    //otherwise, not first time. allocate memory
    //find available space, remove and return it
    size_t block_size = 128;
    int order = 0;
    while (block_size - sizeof(MallocMetadata) < size){
        block_size *= 2;
        order++;
    }

    if (order > MAX_ORDER) return NULL;

    for (int i = order; i <= MAX_ORDER; i++){
        if (memory_order_list[i] == NULL) continue;

        MallocMetadata* node = memory_order_list[i];


        if (node != NULL){
            while (node->order > order){
                split_block(node);
            }

            //remove block from list
            if (node->prev != NULL){
                node->prev->next = node->next;
            }else{
                memory_order_list[order] = node->next;
            }

            if (node->next != NULL){
                node->next->prev = node->prev;
            }

            node->is_free = false;
            node->next = NULL;
            node->prev = NULL;

            return node + 1;
        }
    }

     //no available blocks
        return NULL;
}



void* scalloc(size_t num, size_t size){
    if (size == 0 || num == 0 || num*size > 100000000) return NULL;

    void* space = smalloc(num*size);
    if (space == NULL) return NULL;

    std::memset(space, 0, num*size);

    return space;
}

void merge_buddies(MallocMetadata* block_incoming, MallocMetadata* block_in_list){
    int old_order = block_in_list->order;
    int new_order = old_order + 1;


    //detach the free block
    if (block_in_list->prev != NULL){
        block_in_list->prev->next = block_in_list->next;
    }else{
        memory_order_list[old_order] = block_in_list->next;
    }

    if(block_in_list->next != NULL){
        block_in_list->next->prev = block_in_list->prev;
    }

    //reshape block into one before insertion.
    MallocMetadata* smaller = block_incoming;
    if (block_incoming > block_in_list) smaller = block_in_list;

    smaller->order = new_order;
    smaller->size = block_in_list->size*2 + sizeof(MallocMetadata);
    smaller->next = NULL;
    smaller->prev = NULL;

    total_allocated_blocks--;
    // //insert to list
    // if (memory_order_list[new_order] == NULL){
    //     memory_order_list[new_order] = smaller;
    // }else if (smaller < memory_order_list[new_order]){
    //     smaller->next = memory_order_list[new_order];
    //     memory_order_list[new_order]->prev = smaller;
    //     memory_order_list[new_order] = smaller;
    // }else{
    //      MallocMetadata* temp = memory_order_list[new_order];

    //      while (smaller < temp && temp->next != NULL){
    //         temp = temp->next;
    //      }

    //      if (temp->next != NULL){
    //         smaller->next = temp->next;
    //         temp->next->prev = smaller;
    //      }

    //      temp->next = smaller;
    //      smaller->prev = temp->next;
    // }
}


void sfree(void* p){
    if (p == NULL) return;
    MallocMetadata* space_meta = (MallocMetadata*)p - 1;

    if (space_meta->size + _size_meta_data() > 128*KB){
        //its a mmaped block.
        //detach
        if (space_meta->prev == NULL){
            mapped_memory = space_meta->next;
        }else{
            space_meta->prev->next = space_meta->next;
        }

        if (space_meta->next != NULL){
            space_meta->next->prev = space_meta->prev;
        }

        munmap((void*)space_meta, space_meta->size + _size_meta_data());
        return;
    }

    if (space_meta->is_free) return;
    space_meta->is_free = true;

    //as long as buddies are free, combine. but do not re add the block.
    if (space_meta->order < MAX_ORDER ){
        size_t block_size = 128 * (1 << space_meta->order);
        MallocMetadata* buddy = (MallocMetadata*)((intptr_t)space_meta ^ block_size);


        while (space_meta->order < MAX_ORDER  && buddy->is_free == true && buddy->order == space_meta->order){
            merge_buddies(space_meta, buddy);
            
            if(space_meta > buddy){
                space_meta = buddy;
            }

            block_size = 128 * (1 << space_meta->order);
            buddy = (MallocMetadata*)((intptr_t)space_meta ^ block_size);
        }
    }


    //when here, space_meta hold a valid node that needs to be inserted.
    int order = space_meta->order;

    if (memory_order_list[order] == NULL){
        memory_order_list[order] = space_meta;
    }else if (space_meta < memory_order_list[order]){
        space_meta->next = memory_order_list[order];
        memory_order_list[order]->prev = space_meta;
        memory_order_list[order] = space_meta;
    }else{
         MallocMetadata* temp = memory_order_list[order];

         while (temp->next != NULL && temp->next < space_meta){
            temp = temp->next;
         }

         if (temp->next != NULL){
            space_meta->next = temp->next;
            temp->next->prev = space_meta;
         }

         temp->next = space_meta;
         space_meta->prev = temp;
    }
}


void* srealloc(void* oldp, size_t size){
    if (size == 0 || size > 100000000) return NULL;
    if (oldp == NULL) return smalloc(size);

    MallocMetadata* old_space_meta = (MallocMetadata*)oldp - 1;

    //a. if it already fits. and or not an mmap block of a new size.
    if (old_space_meta->size == size || (size < old_space_meta->size && size + _size_meta_data() <= 128*KB)){
        return oldp;
    }

    //b. check if merging current block produce a large enough block.
    if (size + _size_meta_data() <= 128*KB){
        int cur_order = old_space_meta->order;
        bool is_pos = true;

        MallocMetadata* block = old_space_meta;
        int cur_size = block->size;

        //check if can be merged with buddy.
        while(cur_order < MAX_ORDER && size > cur_size){
            size_t block_size = 128 * (1 << cur_order);
            MallocMetadata* buddy = (MallocMetadata*)((intptr_t)block ^ block_size);
            if (buddy->is_free == false || buddy->order != cur_order){
                is_pos = false;
                break;
            }else if (cur_size*2 + _size_meta_data() >= size){
                break;
            }
            
            if (block > buddy){
                block = buddy;
            }
            cur_order++;
            cur_size = cur_size*2 + _size_meta_data();
            if (cur_order == MAX_ORDER && size > cur_size){
                is_pos = false;
                break;
            }else if (cur_order == MAX_ORDER){
                break;
            }
        }

        //if meging produces a big enough block, then merge and reuse that block.
        //notice: the data is still in the block but might be shifted if merged with buddies in lower adresses.
        //fix: we merge, get the lowest address and then memcopy to it.
        if (is_pos){
            block = old_space_meta;
            cur_size = block->size;
            cur_order = old_space_meta->order;
            size_t original_size = old_space_meta->size;

            while (cur_size < size){
                size_t block_size = 128 * (1 << cur_order);
                MallocMetadata* buddy = (MallocMetadata*)((intptr_t)block ^ block_size);
                merge_buddies(block, buddy);
                if (buddy < block){
                    block = buddy;
                }
                cur_order++;
                cur_size = cur_size*2 + _size_meta_data();
                block->order = cur_order;
                block->size = cur_size;
            }
            //eventually, block hold the block we want, no need to return to free list because it is going to be used.
            //we now copy the data to it, and return.
            std::memmove((void*)(block + 1), oldp, original_size);
            block->is_free = false;
            return (void*)(block + 1);
        }
    }
    
    //c. if here, condition a and b arent valid. so do it normally

    void* new_space = smalloc(size);
    if (new_space == NULL) return NULL;
    if (size > old_space_meta->size){
        std::memmove(new_space, oldp, old_space_meta->size);
    }else{
        std::memmove(new_space, oldp, size);
    }
    sfree(oldp);

    return new_space;
}

size_t _num_free_blocks(){
    size_t count = 0;
    for (int i = 0; i <= MAX_ORDER; i++){
        MallocMetadata* temp = memory_order_list[i];
        while (temp != NULL){
            count++;
            temp = temp->next;
        }
    }

    return count;
}

size_t _num_free_bytes(){
    size_t count = 0;
    for (int i = 0; i <= MAX_ORDER; i++){
        MallocMetadata* temp = memory_order_list[i];
        while (temp != NULL){
            count += temp->size;
            temp = temp->next;
        }
    }

    return count;
}

size_t _num_allocated_blocks(){

    //amount of blocks in buddy allocator.
    size_t amount = total_allocated_blocks;

    MallocMetadata* temp = mapped_memory;
    
    //count mmaped memory blocks
    while (temp != NULL){
        amount += 1;
        temp = temp->next;
    }

    return amount;
}

size_t _num_allocated_bytes(){

    //amount of bytes in buddy allocator
    size_t amount = 128*initial_blocks_amount*KB - total_allocated_blocks*sizeof(MallocMetadata);
    
    //add free bytes from mmaped blocks.
    MallocMetadata* temp = mapped_memory;
    while (temp != NULL){
        amount += temp->size;
        temp = temp->next;
    }

    return amount;
}

size_t _num_meta_data_bytes(){
    return _num_allocated_blocks()*sizeof(MallocMetadata);
}

size_t _size_meta_data(){
    return sizeof(MallocMetadata);
}