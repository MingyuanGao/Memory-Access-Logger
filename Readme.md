MemoryAccessLogger is a Pin tool that logs memory accesses of an application. In particular, this Pin tool can provide statistics for memory-access instructions (memory-read v.s. memory-write instructions), and for memory-access sizes (total size, memory-read size, and memory-write size).

#### How to Compile the Pin Tool
In Makefile, set the variable "PIN_ROOT" to the root directory of the Pin kit.

`$ make`

#### How to Use the Pin Tool
For example, to see the aforementioned statistics for the "ls" command, just run the "ls" command using Pin.

`$ pin -t obj-intel64/MemoryAccessLogger.so -o ls.log -- ls`

`$ cat ls.log`
	

    MemoryAccessLogger Results: 
    ============================================================
    
    Number of instructions executed        : 378236
    ----------------------------------------------------------
    Number of memory-access instructions   : 129874 (34.3368%) 
        Number of memory-read instructions : 94058
            24.8675% of all instructions
            72.4225% of memory-access instructions
        Number of memory-write instructions: 35816
            9.46922% of all instructions
            27.5775% of memory-access instructions
        Number of instructions that ONLY read from memory: 37534
            39.9052% of memory-read instructions
            28.9003% of memory-access instructions
    
    Total Memory Accessed               : 140KB 
    ----------------------------------------------------------
       Size of memory that are read from: 109KB (77.8571%) 
       Size of memory that are written to: 47KB (33.5714%) 
       Size of memory that are ONLY read from: 92KB (65.7143%) 
    ============================================================
    
    
#### MemoryAccessLoggerFast

Sometimes, the analysis process can take many hours. By using all the CPU cores on the host for the analysis part, `MemoryAccessLoggerFast` is much faster than `MemoryAccessLogger`. By default, the analysis process should use all the CPU cores on the host. However, using C++11 threads in a Pin tool is quite tricky. Thus, the analysis process is split from the Pin tool. 

Below commands show how to use `MemoryAccessLoggerFast`.

`$ make`

`$ g++ -std=c++11 -pthread -o analyzer Analyzer.cpp`

`$ pin -t obj-intel64/MemoryAccessLogger.so -o ls.log -- ls`

`$ ./analyzer `
	
	Analyzing ...
	(Be patient, this may take hours!)
	See file ls.log for analysis results!

`$ cat ls.log`

    MemoryAccessLogger Results: 
    ============================================================
    
    Number of instructions executed        : 378236
    ----------------------------------------------------------
    Number of memory-access instructions   : 129874 (34.3368%) 
        Number of memory-read instructions : 94058
            24.8675% of all instructions
            72.4225% of memory-access instructions
        Number of memory-write instructions: 35816
            9.46922% of all instructions
            27.5775% of memory-access instructions
        Number of instructions that ONLY read from memory: 37534
            39.9052% of memory-read instructions
            28.9003% of memory-access instructions
    
    Total Memory Accessed               : 140KB 
    ----------------------------------------------------------
       Size of memory that are read from: 109KB (77.8571%) 
       Size of memory that are written to: 47KB (33.5714%) 
       Size of memory that are ONLY read from: 92KB (65.7143%) 
    ============================================================

