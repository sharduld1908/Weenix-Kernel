# Weenix Kernel Project

This repository contains my implementation for the CSCI 402 - Operating Systems course at the University of Southern California (USC). The project is based on the Weenix educational kernel, and demonstrates my understanding of core operating system concepts through hands-on kernel development.

## Project Overview

The Weenix kernel project required the design and implementation of several key operating system components, including:

- **Process Management:**  
  Implemented process creation, scheduling, context switching, and synchronization primitives (mutexes, condition variables, etc.).

- **Virtual Memory:**  
  Developed virtual memory management, including page tables, address translation, page fault handling, and memory-mapped files.

- **File System (VFS):**  
  Built a virtual file system layer, including file and directory operations, file descriptors, and support for mounting multiple file systems.

- **System Calls:**  
  Implemented a variety of system calls for process, file, and memory management, enabling user programs to interact with the kernel.

- **Device Drivers:**  
  Integrated basic device drivers for terminal and block devices, and implemented special device files (e.g., `/dev/null`, `/dev/zero`, `/dev/tty0`).

## Key Features

- **User Process Support:**  
  The kernel can load and execute user programs, providing isolation and resource management.

- **Robust Virtual Memory:**  
  Supports demand paging, copy-on-write, and memory-mapped files.

- **Comprehensive File System:**  
  Includes support for file creation, deletion, reading, writing, and directory navigation.

- **Testing and Debugging:**  
  Extensively tested using provided test suites and custom test cases. Debugging support is enabled via configurable debug output.

## How to Build and Run

1. **Build the Kernel:**  
   ```sh
   make
   ```

2. **Run Weenix:**  
   ```sh
   ./weenix -n
   ```

3. **Testing:**  
   Run the provided test programs (e.g., `/usr/bin/vfstest`, `/usr/bin/memtest`, `/usr/bin/eatmem`, etc.) from the user shell.

## Project Structure

- `kernel/` - Source code for kernel subsystems:
  - `api/` - System call implementations
  - `fs/` - File system and VFS code
  - `main/` - Kernel entry and initialization
  - `mm/` - Memory management
  - `proc/` - Process and thread management
  - `vm/` - Virtual memory subsystem
- `Config.mk` - Build configuration
- `README.md` - Project documentation
- `weenix-documentation.pdf` - Project Description

## Acknowledgments

This project was developed as part of CSCI 402 at USC. The Weenix kernel framework and test suites were provided by the course staff.
