#include <string.h>
#include <unistd.h>

typedef struct MallocMetaData {
    size_t size;
    bool is_free;
    MallocMetaData* next;
    MallocMetaData* prev;
} MetaDataStruct;

size_t _size_meta_data() {
    return sizeof(MallocMetaData);
}

class BlocksList {
private:
    MetaDataStruct* first_block;
    size_t free_blocks;
    size_t free_bytes;
    size_t allocated_blocks;
    size_t allocated_bytes;

public:
    BlocksList() : first_block(nullptr), free_blocks(0), free_bytes(0), allocated_blocks(0), allocated_bytes(0) {}
    ~BlocksList() = default;

    // inserts new block to the end of the list
    void insert_block(MetaDataStruct *block) {
        if (this->first_block == nullptr) {
            block->next = nullptr;
            block->prev = nullptr;
            block->is_free = false;
            this->first_block = block;
        }
        else {
            MetaDataStruct *tmp = this->first_block;
            while (tmp->next != nullptr) {
                tmp = tmp->next;
            }
            block->next = nullptr;
            block->prev = tmp;
            block->is_free = false;
            tmp->next = block;
        }
        this->allocated_blocks++;
        this->allocated_bytes += block->size - _size_meta_data();
    }

    // updates some free block to be not free anymore. we assume here the new size is smaller than the block
    void set_allocated_block(MetaDataStruct *block) {
        size_t actual_size = block->size - sizeof(MetaDataStruct);
        if (!block->is_free) {
            return;
        }
        block->is_free = false;
        this->free_blocks--;
        this->free_bytes -= actual_size;
        this->allocated_blocks++;
        this->allocated_bytes += actual_size;
    }

    void set_free_block(MetaDataStruct* block) {
        size_t actual_size = block->size - sizeof(MetaDataStruct);
        if (!block->is_free) {
            block->is_free = true;
            this->free_blocks++;
            this->free_bytes += actual_size;
            this->allocated_blocks--;
            this->allocated_bytes -= actual_size;
        }
    }

    MetaDataStruct* get_first() {
        return this->first_block;
    }

    size_t get_free_blocks() {
        return this->free_blocks;
    }
    size_t get_free_bytes() {
        return this->free_bytes;
    }
    size_t get_allocated_blocks() {
        return this->allocated_blocks;
    }
    size_t get_allocated_bytes() {
        return this->allocated_bytes;
    }
};

static BlocksList blocks_list = BlocksList();

void* smalloc(size_t size) {
    if (size <= 0) {
        return nullptr;
    }
    if (size > 100000000) {
        return nullptr;
    }

    MetaDataStruct* tmp = blocks_list.get_first();
    while (tmp != nullptr) {
        if (tmp->is_free && tmp->size - _size_meta_data() >= size) {
            blocks_list.set_allocated_block(tmp);
            return tmp + 1;
        }
        tmp = tmp->next;
    }

    void * ret = sbrk(size + sizeof(MetaDataStruct));
    if (ret == (void*)-1){
        return nullptr;
    }
    MallocMetaData* s = (MallocMetaData*)ret;
    s->size = size + _size_meta_data();
    blocks_list.insert_block(s);
    return s + 1;
}

void* scalloc(size_t num, size_t size) {
    if (num <= 0 || size <= 0 || size > 100000000){
        return nullptr;
    }
    void* p = smalloc(num*size);
    if (p == nullptr) {
        return nullptr;
    }
    memset(p, 0, num*size);
    return p;
}

void sfree(void* p) {
    if (p == nullptr) {
        return;
    }
    MetaDataStruct* block = ((MetaDataStruct*)p) - 1 ;
    blocks_list.set_free_block(block);
}

void* srealloc(void* oldp, size_t size) {
    if (oldp == nullptr) {
        return smalloc(size);
    }
    MetaDataStruct* old = ((MetaDataStruct*)oldp) - 1 ;
    if (size <= 0 || size > 100000000){
        return nullptr;
    }
    size_t actual_old_size = old->size - sizeof(MetaDataStruct);
    if (actual_old_size >= size) {
        return oldp;
    }
    void * res = smalloc(size);
    memmove(res, oldp, actual_old_size);
    sfree(oldp);
    return res;
}

size_t _num_free_blocks() {
    return blocks_list.get_free_blocks();
}
size_t _num_free_bytes() {
    return blocks_list.get_free_bytes();
}
size_t _num_allocated_blocks() {
    return blocks_list.get_allocated_blocks() + blocks_list.get_free_blocks();
}
size_t _num_allocated_bytes() {
    return blocks_list.get_free_bytes() + blocks_list.get_allocated_bytes();
}
size_t _num_meta_data_bytes() {
    return blocks_list.get_free_blocks() * _size_meta_data() + blocks_list.get_allocated_blocks() * _size_meta_data();
}
