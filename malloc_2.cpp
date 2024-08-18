#include <unistd.h>
#include <cstring>

constexpr size_t MAX_ALLOCATION_SIZE = 100000000;

struct MallocMetadata {
    size_t size;
    bool is_free = false;
    MallocMetadata* next = nullptr;
    MallocMetadata* prev = nullptr;
};

constexpr size_t METADATA_SIZE = sizeof(MallocMetadata);

MallocMetadata* memoryBlocks = nullptr;

MallocMetadata* find_first_free(size_t size)
{
    if(memoryBlocks == nullptr || size <= 0)
    {
        return nullptr;
    }
    MallocMetadata* iter = memoryBlocks;
    while(iter->next!= nullptr)
    {
        if(iter->is_free && size <= iter->size)
        {
            return iter;
        }
        iter = iter->next;
    }
    return iter;
}

void* smalloc(size_t size)
{
    if(size == 0 || size > MAX_ALLOCATION_SIZE)
    {
        return nullptr;
    }
    MallocMetadata* block = find_first_free(size);
    //if empty or last and not free
    if(block == nullptr || (block->next == nullptr && !block->is_free))
    {
        void* block_ptr = sbrk(0);
        if(sbrk(sizeof(MallocMetadata) + size) == (void*) - 1)
        {
            return nullptr;
        }
        MallocMetadata* new_block = (MallocMetadata*)block_ptr;
        new_block->size = size;
        new_block->is_free = false;
        //empty
        if(block == nullptr)
        {
            memoryBlocks = new_block;
        }
        //last
        else
        {
            block->next = new_block;
            new_block->prev = block;
        }
        return (void*)((char*)(new_block) + sizeof(MallocMetadata));
    }
    //some free block in the list, removing else because of return
    block->is_free = false;
    return (void*)((char*)(block) + sizeof(MallocMetadata));
}

void* scalloc(size_t num, size_t size)
{
    int real_size = num * size;
    void* block_ptr = smalloc(real_size);
    if(block_ptr != nullptr)
    {
        memset(block_ptr, 0, real_size);
    }
    return block_ptr;
}

void sfree(void *p)
{
    if(p == nullptr)
    {
        return;
    }
    MallocMetadata* block =(MallocMetadata*)((char*)p - sizeof (MallocMetadata));
    if(block->is_free)
    {
        return;
    }
    block->is_free = true;
}

void* srealloc(void* oldp, size_t size)
{
    if(size == 0 || size > MAX_ALLOCATION_SIZE)
    {
        return nullptr;
    }
    if(oldp == nullptr)
    {
        return smalloc(size);
    }
    MallocMetadata* block =(MallocMetadata*)((char*)oldp - sizeof (MallocMetadata));
    if(size <= block->size){
        return oldp;
    }
    void* block_ptr = smalloc(size);
    if(block_ptr != nullptr)
    {
        memmove(block_ptr, oldp, block->size);
        sfree((void*)((char*)(block) + sizeof (MallocMetadata)));
    }
    return block_ptr;
}

size_t _num_free_blocks() {
    size_t size = 0;
    if(memoryBlocks == nullptr)
    {
        return size;
    }
    for(MallocMetadata* iter = memoryBlocks; iter != nullptr; iter = iter->next)
    {
        if(iter->is_free){
            size++;
        }
    }
    return size;
}

size_t _num_free_bytes(){
    size_t size = 0;
    if(memoryBlocks == nullptr)
    {
        return size;
    }

    for(MallocMetadata* iter = memoryBlocks; iter != nullptr; iter = iter->next)
    {
        if(iter->is_free)
        {
            size += iter->size;
        }
    }
    return size;
}

size_t _num_allocated_blocks(){
    size_t size = 0;
    if(memoryBlocks == nullptr)
    {
        return size;
    }

    for(MallocMetadata* iter = memoryBlocks; iter != nullptr; iter = iter->next)
    {
        size++;
    }
    return size;
}

size_t _num_allocated_bytes(){
    size_t size = 0;
    if(memoryBlocks == nullptr)
    {
        return size;
    }

    for(MallocMetadata* iter = memoryBlocks; iter != nullptr; iter = iter->next)
    {
        size+= iter->size;
    }
    return size;
}

size_t _num_meta_data_bytes(){
    return METADATA_SIZE * _num_allocated_blocks();
}

size_t _size_meta_data(){
    return METADATA_SIZE;
}