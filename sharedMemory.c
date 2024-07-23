/*
    This program demonstrates the sharing of memory across processes using Physical memory
    VGA memory at 0xb800000and read/write to it from both parent and client. Must be root 
    to run this program.

    To test concurrently, run 
    
        sudo ./sharedMemory 

    Then run the following from another terminal:
    
        sudo hexdump -C --skip 0xb8000 /dev/mem | head

    You should see the changes made by the client in the VGA memory, in particular the 0xdead value
    at the start of the VGA memory, before the program exits. Once exited, memory is restored.
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


int ChildLogic(){
    int pageSize = sysconf(_SC_PAGE_SIZE);
    printf("Child Starting\n");

    int memFd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memFd == -1){
        printf("Client Error opening /dev/mem\n");
        return 1;
    }
    void *vgaMem = mmap(0, pageSize, PROT_READ | PROT_WRITE, MAP_SHARED, memFd, VGA_MEMBASE);
    if (vgaMem == MAP_FAILED){
        printf("Client Error mapping VGA memory\n");
        return 1;
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
    printf("Child: Exiting\n");
    return 0;
}

int main(int iargs, char **args){
    int childPid;

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
    uint8_t *pxData = (uint8_t*)vgaMem;
    printf("First 16 bytes of Video memory(should be like 0x20,0x00,0x20,0x00):\n ");
    for (int i = 0; i < 16; i++){
        printf("%02X ", pxData[i]);
    }
    printf("\n");


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
     
  
    childPid = fork();
    if (childPid == -1){
        printf("Error forking child process\n");
        exit(1);
    }
    if (childPid == 0){
        ChildLogic();
    } else {
        // Parent process
        // Wait for child to finish
        sleep(1);

        // See if client changes reflect in vga memory.
        if (vgaPtr[0] == 0xdead){
            printf("Parent: Child changes reflected in VGA memory\n");
        } else {
            printf("Parent: Child changes not reflected in VGA memory\n");
        }
        sleep(10);
        memcpy(vgaMem, t, pageSize);  // restore the VGA memory
        // Close the file descriptor
        close(memFd);

    }
    return 0;
}


