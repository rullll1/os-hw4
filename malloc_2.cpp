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

MallocMetadata* memory_blocks = nullptr;

MallocMetadata* allocate(const size_t size)
{
    if(memory_blocks == nullptr || size <= 0)
    {
        return nullptr;
    }
    MallocMetadata* curr = memory_blocks;
    while(curr->next != nullptr)
    {
        if(curr->is_free && size <= curr->size)
        {
            return curr;
        }
        curr = curr->next;
    }
    return curr;
}

void* smalloc(size_t size) {
    if (size == 0 || size > MAX_ALLOCATION_SIZE) {
        return nullptr;
    }

    MallocMetadata* block = allocate(size);
    if (block == nullptr || (block->next == nullptr && !block->is_free)) {
        void* block_ptr = sbrk(0);
        if (sbrk(METADATA_SIZE + size) == reinterpret_cast<void *>(-1)) {
            return nullptr;
        }

        auto* new_block = static_cast<MallocMetadata *>(block_ptr);
        new_block->size = size;

        if (block == nullptr) {
            memory_blocks = new_block;
        } else {
            block->next = new_block;
            new_block->prev = block;
        }

        return reinterpret_cast<char *>(new_block) + METADATA_SIZE;
    }

    block->is_free = false;
    return reinterpret_cast<char *>(block) + METADATA_SIZE;
}

void* scalloc(size_t num, size_t size) {
    size_t mem_size = num * size;
    void* block_ptr = smalloc(mem_size);
    if (block_ptr != nullptr) {
        std::memset(block_ptr, 0, mem_size);
    }
    return block_ptr;
}

void sfree(void* p) {
    if (p == nullptr) {
        return;
    }

    auto* block = reinterpret_cast<MallocMetadata *>(static_cast<char *>(p) - METADATA_SIZE);
    block->is_free = true;
}

void* srealloc(void* oldp, size_t size) {
    if (size == 0 || size > MAX_ALLOCATION_SIZE) {
        return nullptr;
    }

    if (oldp == nullptr) {
        return smalloc(size);
    }

    auto* block = reinterpret_cast<MallocMetadata *>(static_cast<char *>(oldp) - METADATA_SIZE);

    if (size <= block->size) {
        return oldp;
    }

    void* new_block = smalloc(size);
    if (new_block != nullptr) {
        std::memmove(new_block, oldp, block->size);
        sfree(oldp);
    }

    return new_block;
}

size_t _num_free_blocks() {
    size_t count = 0;
    for (const MallocMetadata* iter = memory_blocks; iter != nullptr; iter = iter->next) {
        if (iter->is_free) {
            count++;
        }
    }
    return count;
}

size_t _num_free_bytes() {
    size_t total_size = 0;
    for (const MallocMetadata* iter = memory_blocks; iter != nullptr; iter = iter->next) {
        if (iter->is_free) {
            total_size += iter->size;
        }
    }
    return total_size;
}

size_t _num_allocated_blocks() {
    size_t count = 0;
    for (const MallocMetadata* iter = memory_blocks; iter != nullptr; iter = iter->next) {
        count++;
    }
    return count;
}

size_t _num_allocated_bytes() {
    size_t total_size = 0;
    for (const MallocMetadata* iter = memory_blocks; iter != nullptr; iter = iter->next) {
        total_size += iter->size;
    }
    return total_size;
}

size_t _num_meta_data_bytes() {
    return METADATA_SIZE * _num_allocated_blocks();
}

size_t _size_meta_data() {
    return METADATA_SIZE;
}