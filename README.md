# Memory Mapping for linux

Demonstrate sharing of locked memory between processes.

1. sharedMemory.c will map the old VGA at 0x0b8000, save 4kb to local buffer, write and read between a child and parent process and then restore and quit. This requires root privileges.

    To test concurrently, run 
    
        sudo ./sharedMemory 

    Then run the following from another terminal:
    
        sudo hexdump -C --skip 0xb8000 /dev/mem | head

2. Demonstrate use and locking of shared virtual memory and lock it to prevent paging. Parent and child will alternate concatenating a string and then the parent will print it when all is done. This does not require elevated privileges.
   


