# Multi-Process IPC File Analyzer 
This program analyzes letter frequencies in multiple files using concurrent child processes. Each child reads a file, calculates a histogram of letters (a–z), and sends it to the parent via a pipe. The parent handles SIGCHLD signals to detect terminated children, reads their histograms, and saves the results to files.

# Key OS Concepts Applied

Process creation (fork())
Inter-process communication (pipes)
Signal handling (SIGCHLD)
Concurrency with multiple child processes

# Compiling

run "make"

# Running the program

./parallel test.txt


