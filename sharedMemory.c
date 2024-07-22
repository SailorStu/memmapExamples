/*
    This program demonstrates the sharing of memory across processes.
    It will create a shared memory area and then fork a chld process.
    Both processes will continue to increment a counter and update the message
    in the shared memory area. The parent process will run and then terminate 
    after 20 seconds. The Child process will detect such and exit.

    It also demonstrates the use of mmap to map the physical VGA memory at 0xb80000 
    and read/write to it from both parent and client. Must be root to run this program.

*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>



#define _GNU_SOURCE   // Needed for get and set affinity


#define VGA_MEMBASE 0x000B8000

typedef struct SharedMemory {
    pthread_mutex_t mutex;
    int             counter;
    bool            running;
    bool            childDone;
    int             turn;
    char            message[64];
} SharedMemory;


void ChildLogic(SharedMemory *shared){
    struct timespec t;
    int pageSize = sysconf(_SC_PAGE_SIZE);
    t.tv_sec = 0;
    t.tv_nsec = 25000;
    printf("Child Starting\n");

    int memFd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memFd == -1){
        printf("Client Error opening /dev/mem\n");
        exit(1);
    }
    void *vgaMem = mmap(0, pageSize, PROT_READ | PROT_WRITE, MAP_SHARED, memFd, VGA_MEMBASE);
    if (vgaMem == MAP_FAILED){
        printf("Client Error mapping VGA memory\n");
        exit(1);
    } else {
        printf("Client VGA Memory mapped at %p\n", vgaMem);
    }
    // Read from the VGA memory
    uint16_t *vgaPtr = (uint16_t*)vgaMem;
    if (vgaPtr[0] == 0x0f55 && vgaPtr[1] == 0){
        printf("Client VGA Memory read successful\n");
    } else {
        printf("Client VGA Memory read failed %04x, %04x\n", vgaPtr[0], vgaPtr[1]);
    }
    vgaPtr[0] = 0xdead;
    // close the VGA memory
    close(memFd);


    while(shared->running){
        pthread_mutex_lock(&shared->mutex);
        if(shared->turn == 1){
            sprintf(shared->message, "Child: Counter: %d", shared->counter);
            shared->turn = 0;
        }
        pthread_mutex_unlock(&shared->mutex);
        nanosleep(&t, NULL); // Sleep for 50ms
    }
    shared->childDone = true;
    printf("Child: Exiting\n");
    exit(0);
}


void ParentLogic(SharedMemory *shared){
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = 50000;

    while(shared->running){
        printf("%s\n", shared->message);
        pthread_mutex_lock(&shared->mutex);
        if(shared->turn == 0){
            sprintf(shared->message, "Parent: Counter: %d", shared->counter);
            shared->turn = 1;
        } 
        shared->counter++;
        pthread_mutex_unlock(&shared->mutex);
        nanosleep(&t,NULL); // Sleep for 50ms
       
        if(shared->counter > 20){
            shared->running = false;
        }
    }
    printf("Parent: Exiting\n");
}


SharedMemory *shPtr;



int main(int iargs, char **args){
    int childPid;
    int shMemFd;

    int pageSize = sysconf(_SC_PAGE_SIZE);

    // are we root?
    if (geteuid() != 0){
        printf("You must be root to run this program\n");
        exit(1);
    }
    int memFd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memFd == -1){
        printf("Error opening /dev/mem\n");
        exit(1);
    }
    // Map the VGA memory
    void *vgaMem = mmap(0, pageSize, PROT_READ | PROT_WRITE, MAP_SHARED, memFd, VGA_MEMBASE);
    if (vgaMem == MAP_FAILED){
        printf("Error mapping VGA memory\n");
        exit(1);
    } else {
        printf("VGA Memory mapped at %p\n", vgaMem);
    }
    // Write to the VGA memory
    uint16_t *vgaPtr = (uint16_t*)vgaMem;
    uint16_t t[pageSize];
    memcpy(t, vgaMem, pageSize);
    memset(vgaPtr, 0, pageSize);
    vgaPtr[0] = 0x0F55;
    sleep(1);
    if (vgaPtr[0] == 0x0F55 && vgaPtr[1] == 0){
        printf("VGA Memory write successful\n");
    } else {
        printf("VGA Memory write failed %04x, %04x\n", vgaPtr[0], vgaPtr[1]);
    }
    
    // Create shared memory area
    shMemFd = shm_open("/sharedMemory", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (shMemFd == -1){
        printf("Error creating shared memory\n");
        exit(1);
    }
    // allocate a size for shared memory
    if (ftruncate(shMemFd, sizeof(SharedMemory)) == -1){
        printf("Error truncating shared memory\n");
        exit(1);
    }
    // Map shared memory to process parent
    shPtr = (SharedMemory*)mmap(0, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shMemFd, 0);
    if (shPtr == MAP_FAILED){
        printf("Error mapping shared memory\n");
        exit(1);
    }
    // lock the shared memory to prevent paging
    if (mlock(shPtr, sizeof(SharedMemory)) != 0){
        printf("Error locking shared memory\n");
        exit(1);
    }   

    pthread_mutex_init(&shPtr->mutex, NULL);
    shPtr->counter = 0;
    shPtr->childDone = false;
    shPtr->running = true;
    shPtr->turn = 0;
    strcpy(shPtr->message, "Initializing");

    childPid = fork();
    if (childPid == -1){
        munlock(shPtr, sizeof(SharedMemory));
        munmap(shPtr, sizeof(SharedMemory));
        printf("Error forking child process\n");
        exit(1);
    }
    if (childPid == 0){
        // Since child is copy on write, we need to create a pointer to the passed shared memory area.
        shPtr = (SharedMemory*)mmap(0, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shMemFd, 0);
        ChildLogic(shPtr);
    } else {
        // Parent process
        ParentLogic(shPtr);
        // Wait for child to finish
        int retries = 3;
        while(!shPtr->childDone && retries > 0){
            sleep(1);
            retries--;
        }
        if (retries == 0){
            printf("Child did not finish. Terminating child.\n");    
            // kill the child pid
            kill(childPid, 9);
        }
    
        // Unlock and unmap shared memory
        munlock(shPtr, sizeof(SharedMemory));
        munmap(shPtr, sizeof(SharedMemory));
        // Close shared memory
        shm_unlink("/sharedMemory");


        // See if client changes reflect in vga memory.
        if (vgaPtr[0] == 0xdead){
            printf("Parent: Client changes reflected in VGA memory\n");
        } else {
            printf("Parent: Client changes not reflected in VGA memory\n");
        }   
        memcpy(vgaMem, t, pageSize);  // restore the VGA memory
        // Close the file descriptor
        close(memFd);

    }
    return 0;
}


