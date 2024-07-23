/*
    This program demonstrates the sharing of memory across processes using posix shared memory.
    It will create a parent and child process that will share a memory area. The parent process
    will create a shared memory area and then fork a chld process. The child process will then
    open the shared memory, append to it, and then exit. The parent process will then read the
    updated shared memory and print it out. The parent process will then unlink the shared memory, closing it. 
   
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

#define SHM_NAME "/sharedMemory1234"

typedef struct SharedMemory {
    pthread_mutex_t mutex;   // Mutex for shared memory
    int             state;   // 0 = parent, 1 = child,2 = done
    char            message[256];
} SharedMemory;


int ChildLogic(SharedMemory *shared){
    struct timespec t;
    int pageSize = sysconf(_SC_PAGE_SIZE);
    t.tv_sec = 0;
    t.tv_nsec = 25000;
    printf("Child Starting\n");
    while(shared->state < 4){
        pthread_mutex_lock(&shared->mutex);
        switch (shared->state){
            case 1:
                shared->state++;
                strcat(shared->message, " Child says hello.");
                break;
            case 3:
                strcat(shared->message, " Child says goodbye.");
                shared->state++;
                break;
        }
        pthread_mutex_unlock(&shared->mutex);
        nanosleep(&t, NULL); // Sleep for 50ms
    }
    printf("Child: Exiting\n");
    return 0;
}


int ParentLogic(SharedMemory *shared){
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = 50000;
    int wd = 0;
    printf("Parent Starting\n");

    while(shared->state < 4){
        pthread_mutex_lock(&shared->mutex);
        switch(shared->state){
            case 0:
                shared->state++;
                strcat(shared->message, " Parent says hello.");
                break;
            case 2:
                strcat(shared->message, " Parent says goodbye.");
                shared->state++;
                break;
        }        
        pthread_mutex_unlock(&shared->mutex);
        nanosleep(&t,NULL); // Sleep for 50ms
        if (wd++ > 50){
            printf("Parent: Child is not responding.\n");
            break;
        }
    }
    printf("Message Buffer:%s\n", shared->message);
    printf("Parent: Exiting\n");
    return 0;
}






int main(int iargs, char **args){
    int childPid;
    SharedMemory *shPtr;
    /*
        Open shared memory area and if exists, truncate it. Ensure read/write permissions for
        current user(non-root). If you want to share across group or others, you need to change
        the permissions(see fstat.h for more details).
    */

    int fd;
    
    fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_SYNC | O_TRUNC, S_IRUSR | S_IWUSR);

    if (fd == -1){
        printf("Error creating shared memory\n");
        exit(1);
    }
    
    if (ftruncate(fd, sizeof(SharedMemory)) == -1){
        printf("Error truncating shared memory\n");
        shm_unlink(SHM_NAME);  // remove the shared memory on error.
        exit(1);
    }
    // Get our pointer to the shared memory.
    void *pMem = mmap(0, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pMem == MAP_FAILED){
        printf("Error mapping shared memory\n");
        shm_unlink(SHM_NAME);  // remove the shared memory on error.
        exit(1);
    }   

    shPtr = (SharedMemory*)pMem;
 
    // lock the shared memory to prevent paging
    if (mlock(shPtr, sizeof(SharedMemory)) != 0){
        printf("Error locking shared memory\n");
        shm_unlink(SHM_NAME);  // remove the shared memory on error.
        exit(1);
    }   

    pthread_mutex_init(&shPtr->mutex, NULL);
    shPtr->state = 0;
    strcpy(shPtr->message, "Good Morning.");

    childPid = fork();
    if (childPid == -1){
        munlock(shPtr, sizeof(SharedMemory));
        munmap(shPtr, sizeof(SharedMemory));
        shm_unlink(SHM_NAME);
        close(fd);
        printf("Error forking child process\n");
        exit(1);
    }
    if (childPid == 0){
        // Since child is copy on write, we need to create a pointer to the passed shared memory area.
        fd = shm_open(SHM_NAME, O_RDWR | O_SYNC, S_IRUSR | S_IWUSR);

        if (fd == -1){
            printf("Child Error creating shared memory\n");
            exit(1);
        }
    
        if (ftruncate(fd, sizeof(SharedMemory)) == -1){
            printf("Child Error truncating shared memory\n");
            shm_unlink(SHM_NAME);  // remove the shared memory on error.
            close(fd);
            exit(1);
        }
        // Get our pointer to the shared memory.
        void *pMem = mmap(0, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (pMem == MAP_FAILED){
            printf("Error mapping shared memory\n");
            shm_unlink(SHM_NAME);  // remove the shared memory on error.
            close(fd);
            exit(1);
        }   
        // lock the shared memory to prevent paging
        if (mlock(pMem, sizeof(SharedMemory)) != 0){
            printf("Client Error locking shared memory\n");
            munmap(pMem, sizeof(SharedMemory));
            shm_unlink(SHM_NAME);  // remove the shared memory on error.
            close(fd);
            exit(1);
        }   
        shPtr = (SharedMemory*)pMem;
        ChildLogic(shPtr);
        munlock(shPtr, sizeof(SharedMemory));
        munmap(shPtr, sizeof(SharedMemory));
        shm_unlink(SHM_NAME);
        close(fd);
    } else {
        // Parent process
        ParentLogic(shPtr);
        // Wait for child to finish
        int retries = 3;
        while(shPtr->state < 4 && retries > 0){
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
        close(fd);
    }
    return 0;
}


