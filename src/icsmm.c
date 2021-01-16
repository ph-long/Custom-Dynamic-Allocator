/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 * If you want to make helper functions, put them in helpers.c
 */
#include "icsmm.h"
#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

ics_free_header *freelist_head = NULL;
void * bottomOfHeap = NULL;
ics_footer * endOfPage = NULL;
long int heapSize;

void *ics_malloc(size_t size) { 
    if (size == 0) {
        errno = EINVAL;
        return NULL; 
    }
    int origSize = size;
    /* If malloc less than 16 bytes, allocate 16 bytes.
    Make the malloc size a factor of 16 if it is not. 
    Lastly add on the size of footer and header */
    if (size < 16) {
        size = 16;
    }
    while (size % 16 != 0)
    {
        size++;
    }
    long int adjustedSize = 8 + size + 8;
    void * pagePointer = NULL;
    if (freelist_head == NULL){

        /* Only if there is no space to malloc */
        int i = adjustedSize / 4096;
        heapSize = (i + 1) * 4096;
        if (heapSize <= 24576) {
            bottomOfHeap = ics_inc_brk(i + 1);

            /* Prologue */
            bottomOfHeap = bottomOfHeap + 8;
            ics_free_header* header = bottomOfHeap;
            void *pageEnd = ics_get_brk();
            ics_footer * footer = pageEnd - 16;
            endOfPage = footer;

            /* Page size minus prologue and epilogue */
            long int pageSize = 4096 * (i + 1);
            footer->block_size = pageSize - 16;
            header->header.block_size = pageSize -16;
            header->header.hid = HEADER_MAGIC;
            footer->fid = FOOTER_MAGIC;
            header->next = NULL;
            header->prev = NULL;
            freelist_head = header;

        } else {
            errno = ENOMEM;
            return NULL;
        }
    }
    pagePointer = freelist_head;
    ics_free_header * current_head = freelist_head;
    while (current_head != NULL)
    {
        if (adjustedSize <= current_head->header.block_size) {

            /* If after splitting, the new block is >= 36 */
            if (current_head->header.block_size - adjustedSize >= 32) {

                /* Set up a new header for splitting */
                ics_free_header* newHeader = pagePointer + adjustedSize;
                newHeader->header.block_size = current_head->header.block_size - adjustedSize;
                newHeader->header.hid = HEADER_MAGIC;
                newHeader->header.block_size ^= (0 ^ current_head->header.block_size) & (1UL << 0); 
                ics_footer * splitFoot = pagePointer + current_head->header.block_size - 8;
                splitFoot->block_size = current_head->header.block_size - adjustedSize;

                /* Allocate Block and create the footer to the block */
                current_head->header.block_size = adjustedSize;
                current_head->header.padding_amount = adjustedSize - 16 - origSize;
                ics_footer * footer = (pagePointer + adjustedSize - 8);
                footer->fid = FOOTER_MAGIC;
                footer->block_size = adjustedSize;

                /* Allocate Bit becomes 1 */
                current_head->header.block_size ^= (-1 ^ current_head->header.block_size) & (1UL << 0);
                footer->block_size ^= (-1 ^ footer->block_size) & (1UL << 0);

                /* Free the current head from the linked list */
                if (current_head == freelist_head) {
                    freelist_head = freelist_head->next;
                } else if (current_head->next == NULL) {
                    current_head->prev->next = NULL;
                } else {
                    current_head->prev->next = current_head->next;
                    current_head->next->prev = current_head->prev;
                }

                /* Add the new empty block to front of list */ 
                // Need to remove the freelist_head
                newHeader->next = freelist_head;
                newHeader->prev = NULL;
                if (newHeader->next != NULL) {
                    newHeader->next->prev = newHeader;
                }
                freelist_head = newHeader;

                return pagePointer + 8;

            // if blocksize - size < 36
            } else {
                ics_footer * footer = (pagePointer + current_head->header.block_size - 8);

                current_head->header.padding_amount = current_head->header.block_size - 16 - origSize;
                current_head->header.block_size ^= (-1 ^ current_head->header.block_size) & (1UL << 0);
                footer->block_size = current_head->header.block_size;
                footer->block_size ^= (-1 ^ footer->block_size) & (1UL << 0);
                if (current_head == freelist_head) {
                    freelist_head = freelist_head->next;
                } else if (current_head->next == NULL) {
                    current_head->prev->next = NULL;
                } else {
                    current_head->prev->next = current_head->next;
                    current_head->next->prev = current_head->prev;
                }
                
                return pagePointer + 8;
            }
        }
        
        current_head = current_head->next;
        pagePointer = current_head;
    }

    /* Extend the Heap */
    int i = adjustedSize / 4096;
    heapSize += i * 4096;
    if (heapSize <= 24576) {

        /* Turn the previous epilogue to be the head, get the new end to the heap
        and make a new footer there */
        bottomOfHeap = ics_inc_brk(i + 1);
        bottomOfHeap += 8;
        ics_free_header * extendedHeapHead = (void *)endOfPage + 8;
        void *pageEnd = ics_get_brk();
        ics_footer * footer = pageEnd - 16;
        endOfPage = footer;
        long int pageSize = 4096 * (i + 1);
        footer->block_size = pageSize - 8;
        extendedHeapHead->header.block_size = pageSize;
        extendedHeapHead->header.hid = HEADER_MAGIC;
        footer->fid = FOOTER_MAGIC;
        extendedHeapHead->next = freelist_head;
        extendedHeapHead->prev = NULL;
        
        freelist_head = extendedHeapHead;

        /* Try malloc again */
        return ics_malloc(size);
    } else {
        errno = ENOMEM;
        return NULL;
    }

    return pagePointer + 8;
}

void *ics_realloc(void *ptr, size_t size) { 

    /* Check for valid pointer */
    if (bottomOfHeap > ptr || ptr > (void *)endOfPage) {
        errno = EINVAL;
        return NULL;
    }
    ics_free_header * ptr_head = ptr - 8;
    if (ptr_head->header.block_size & 0) {
        errno = EINVAL;
        return NULL;
    }
    ics_footer * footerCheck = ptr + ptr_head->header.block_size - 17;
    if (footerCheck->block_size & 0) {
        errno = EINVAL;
        return NULL;
    }
    if (ptr_head->header.hid != HEADER_MAGIC || footerCheck->fid != FOOTER_MAGIC) {
        errno = EINVAL;
        return NULL;
    }
    if (ptr_head->header.block_size != footerCheck->block_size) {
        errno = EINVAL;
        return NULL;
    }


    if (size == 0) {
        ics_free(ptr);
        return NULL;
    }
    long int reallocSize = size;
    if (size < 16) {
        size = 16;
    }
    while (size % 16 != 0)
    {
        size++;
    }
    long int adjustedSize = 8 + size + 8;
    

    /* Check for coalesce */
    ics_free_header* nextHead = ptr + ptr_head->header.block_size - 9;
    if (nextHead->header.block_size & 1) {
    } else {
        ics_free_header* currentList = freelist_head;

        /* Frees block */
        while (currentList != NULL)
        {
            if (currentList == nextHead) {
                if (currentList->prev == NULL)
                {
                    freelist_head = currentList->next;
                } else if (currentList->next == NULL) {
                    currentList->prev->next = NULL;
                } else {
                    currentList->prev->next = currentList->next;
                    currentList->next->prev = currentList->prev;
                }
                break;
            }
            currentList = currentList->next;
        }
        /* Block size is sum of two free blocks */
        ptr_head->header.block_size = ptr_head->header.block_size + nextHead->header.block_size;
        ics_footer * newFooter = (void *)ptr_head + ptr_head->header.block_size - 16;
        newFooter->block_size = ptr_head->header.block_size;
    }
    
    /* If size is smaller or equal to ptr_head's block size*/
    if (adjustedSize <= ptr_head->header.block_size) {

        /* If the block cannot be splinntered off */
        if (ptr_head->header.block_size - adjustedSize - 1 < 32) {

            /* Adjust padding for the new size */
            int originalSize = ptr_head->header.block_size - 16 - ptr_head->header.padding_amount;
            ptr_head->header.padding_amount += (originalSize - reallocSize);
            return ptr;
        }
        /* Splinnter off the remaining block */
        else {

            /* Set up newHeader for the splinntered block
            Fix the footer to match the newly splinntered block*/
            ics_free_header* newHeader = (void *)ptr_head + adjustedSize;
            newHeader->header.block_size = ptr_head->header.block_size - adjustedSize;
            newHeader->header.hid = HEADER_MAGIC;
            newHeader->header.block_size ^= (0 ^ ptr_head->header.block_size) & (1UL << 0);

            ics_footer* fixFooter = (void *)newHeader + newHeader->header.block_size - 8;
            fixFooter->block_size = ptr_head->header.block_size - adjustedSize - 1;

            /* Adjust ptr_head's block size to the adjusted size
            Get the matching padding for the block
            Create a footer to the new block */
            ptr_head->header.block_size = adjustedSize;
            ptr_head->header.padding_amount = adjustedSize - 16 - reallocSize;
            ics_footer * footer = (void *)ptr_head + adjustedSize - 8;
            footer->fid = FOOTER_MAGIC;
            footer->block_size = adjustedSize; 

            /* Turn the allocate bit to 1 for the footer */
            ptr_head->header.block_size ^= (-1 ^ footer->block_size) & (1UL << 0);
            footer->block_size ^= (-1 ^ footer->block_size) & (1UL << 0);

            /* Add the splinntered block to the front of the linked list */
            newHeader->next = freelist_head;
            newHeader->prev = NULL;
            if (newHeader->next != NULL)
            {
                newHeader->next->prev = newHeader;
            }
            freelist_head = newHeader;
            return ptr;
        }
    } else {
        void * enoughSpace = ics_malloc(size);
        void * ptrCopy = ptr;
        if (enoughSpace) {
            memcpy(enoughSpace, ptr, ptr_head->header.block_size - 8);
            ics_free(ptr);
            return enoughSpace;
        } else {
            errno = ENOMEM;
            return NULL;
        }
    }

    return NULL; 
}

int ics_free(void *ptr) { 

    /* Check if ptr points to valid allocated block */
    if (bottomOfHeap <= ptr && ptr <= (void *)endOfPage)
    {
        ics_free_header* header = ptr - 8;
        if (header->header.block_size & 1) {
            header->header.block_size ^= (0 ^ header->header.block_size) & (1UL << 0);
            ics_footer * footer = ptr + header->header.block_size - 16;
            if (footer->block_size & 1) {
                footer->block_size ^= (0 ^ footer->block_size) & (1UL << 0);
                if (header->header.hid == HEADER_MAGIC && footer->fid == FOOTER_MAGIC) {
                    if (header->header.block_size == footer->block_size) {
                        
                        /* Check for coalesce */
                        ics_free_header* nextHead = ptr + header->header.block_size - 8;
                        if (nextHead->header.block_size & 1) {    
                            header->next = freelist_head;
                            header->prev = NULL;
                            freelist_head = header;
                            if (header->next) {
                                header->next->prev = header;
                            }
                            return 0;
                        } else {
                            ics_free_header* currentList = freelist_head;

                            /* Free the nextHeader from the linked list */
                            while (currentList != NULL)
                            {
                                if (currentList == nextHead) {
                                    if (currentList->prev == NULL)
                                    {
                                        freelist_head = currentList->next;
                                    } else if (currentList->next == NULL) {
                                        currentList->prev->next = NULL;
                                    } else {
                                        currentList->prev->next = currentList->next;
                                        currentList->next->prev = currentList->prev;
                                    }
                                    break;
                                }
                                currentList = currentList->next;
                            }

                            /* Add that block to the blocksize of the header and footer*/
                            header->header.block_size = header->header.block_size + nextHead->header.block_size;
                            ics_footer * newFooter = ptr + header->header.block_size - 16;
                            newFooter->block_size = header->header.block_size;

                            /* Add to list */
                            header->next = freelist_head;
                            header->prev = NULL;
                            freelist_head = header;
                            if (header->next) {
                                header->next->prev = header;
                            }
                            return 0;
                        }
                    }
                }
            }
        }
    }
    errno = EINVAL;
    return -1; 
}