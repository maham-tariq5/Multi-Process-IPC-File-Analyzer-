#define _POSIX_C_SOURCE 200809L  // Ensures POSIX.1-2008 compatibility for functions like sigaction, lseek
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1000 // Buffer size for reading data and lines
#define MAX_FILES 100    // Maximum number of files the program can process

// Function prototypes
void sigchld(int sig);              // Signal handler for SIGCHLD
int *Histogram(char *Data, int Size); // Calculate histogram of letters

// Global variables
int pipes[MAX_FILES][2]; // Array of pipes for each child process
int pids[MAX_FILES];     // Array to store PIDs of child processes
int numChildren = 0;     // Number of child processes created
int numTerminated = 0;   // Number of child processes that have terminated

/**
 * SIGCHLD handler: called when a child process terminates.
 * Reads histogram data from the corresponding pipe and saves it to a file.
 */
void sigchld(int sig) {
    int child_status;
    int characterCounts[26]; // Array to store letter counts (a-z)
    char filename[BUFFER_SIZE]; // File name to save histogram
    char line[BUFFER_SIZE];     // Line to write to file
    pid_t child_pid;

    // Loop to process all terminated children without blocking
    while ((child_pid = waitpid(-1, &child_status, WNOHANG)) > 0) {
        printf("Parent caught SIGCHLD from child process %d.\n", child_pid);
        numTerminated++;

        // Check if child exited normally
        if (WIFEXITED(child_status)) {
            int curPipe = -1; // Initialize pipe index

            // Find the pipe corresponding to this child PID
            for (int i = 0; i < MAX_FILES; i++) {
                if (child_pid == pids[i]) {
                    curPipe = i;
                    break;
                }
            }

            // If a valid pipe is found, read histogram data
            if (curPipe != -1) {
                ssize_t bytesRead = read(pipes[curPipe][0], characterCounts, sizeof(characterCounts));
                if (bytesRead > 0) {
                    printf("Parent read histogram from pipe %d ", curPipe);

                    // Prepare filename and open it for writing
                    sprintf(filename, "file%d.hist", child_pid);
                    int fd = open(filename, O_CREAT | O_WRONLY, 0644);
                    if (fd == -1) {
                        perror("Error opening file");
                        exit(EXIT_FAILURE);
                    }

                    // Write character counts (a-z) to file
                    for (char letter = 'a'; letter <= 'z'; letter++) {
                        int count = characterCounts[letter - 'a'];
                        sprintf(line, "%c=%d\n", letter, count);
                        write(fd, line, strlen(line));
                    }
                    close(fd);
                    printf("and saved to file %s.\n", filename);
                }
                close(pipes[curPipe][0]); // Close read end of the pipe
            } else {
                printf("Error: Pipe for child %d not found.\n", child_pid);
            }
        } else if (WIFSIGNALED(child_status)) {
            printf("Child %d terminated abnormally.\n", child_pid);
        }
    }

    // Re-register the SIGCHLD handler
    if (signal(SIGCHLD, sigchld) == SIG_ERR) {
        perror("Error re-registering SIGCHLD handler");
        exit(EXIT_FAILURE);
    }
}

/**
 * Histogram function: calculates frequency of letters (a-z) in input data.
 * @param Data Pointer to input character array
 * @param Size Size of input data
 * @return Pointer to dynamically allocated array of size 26 containing counts
 */
int *Histogram(char *Data, int Size) {
    int *histogram = (int *)malloc(26 * sizeof(int));
    if (!histogram) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    // Initialize all counts to 0
    for (int i = 0; i < 26; i++) histogram[i] = 0;

    // Count letters (case-insensitive)
    for (int i = 0; i < Size; i++) {
        char c = Data[i];
        if (isalpha(c)) {
            c = tolower(c);
            histogram[c - 'a']++;
        }
    }

    return histogram;
}

int main(int argc, char *argv[]) {
    printf("Starting program. Number of files provided: %d\n", argc - 1);

    // Validate input arguments
    if (argc == 1) {
        printf("Error: No input files provided.\n");
        exit(EXIT_FAILURE);
    }
    if (argc > MAX_FILES + 1) {
        printf("Error: Too many input files provided. Maximum allowed is %d.\n", MAX_FILES);
        exit(EXIT_FAILURE);
    }

    // Register SIGCHLD handler using sigaction
    printf("Registering SIGCHLD handler...\n");
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld;
    sigaction(SIGCHLD, &sa, NULL);

    // Loop over each file/command provided in arguments
    for (int i = 1; i < argc; i++) {
        printf("Processing file/command %s...\n", argv[i]);

        // Create a pipe for this child
        if (pipe(pipes[i - 1]) < 0) {
            perror("Error creating pipe");
            exit(EXIT_FAILURE);
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("Error forking child process");
            exit(EXIT_FAILURE);
        } else if (pid == 0) { // Child process
            printf("Child process started for %s\n", argv[i]);
            close(pipes[i - 1][0]); // Close read end in child

            // If the argument is not "SIG", process it as a file
            if (strcmp(argv[i], "SIG") != 0) {
                printf("Opening file: %s\n", argv[i]);
                int fileDescriptor = open(argv[i], O_RDONLY);
                if (fileDescriptor < 0) {
                    fprintf(stderr, "Error opening file %s. Exiting with 1.\n", argv[i]);
                    close(pipes[i - 1][1]);
                    exit(1);
                }

                // Read file content into memory
                off_t fileSize = lseek(fileDescriptor, 0, SEEK_END);
                char *fileData = (char *)malloc(fileSize);
                if (!fileData) {
                    perror("Failed to allocate memory for file data");
                    close(pipes[i - 1][1]);
                    exit(1);
                }
                lseek(fileDescriptor, 0, SEEK_SET);
                read(fileDescriptor, fileData, fileSize);
                close(fileDescriptor);

                // Calculate histogram and write to pipe
                printf("Calculating histogram for file: %s\n", argv[i]);
                int *calcHisto = Histogram(fileData, fileSize);
                write(pipes[i - 1][1], calcHisto, 26 * sizeof(int));
                free(calcHisto);
                free(fileData);

                // Optional sleep to simulate delay
                printf("Child process sleeping for %d seconds.\n", 10 + 3 * (i - 1));
                sleep(10 + 3 * (i - 1));

                printf("Child process completed for %s. Exiting with 0.\n", argv[i]);
                close(pipes[i - 1][1]);
                exit(0);
            } else { // If argument is "SIG", wait for signal
                printf("Child process (PID: %d) waiting for signal.\n", getpid());
                sleep(10);
            }
        } else { // Parent process
            printf("Parent process created child with PID: %d for %s\n", pid, argv[i]);
            close(pipes[i - 1][1]); // Close write end in parent
            pids[i - 1] = pid;
            numChildren++;

            // If argument is "SIG", immediately send SIGINT to child
            if (strcmp(argv[i], "SIG") == 0) {
                printf("Parent sending SIGINT to child %d\n", pid);
                kill(pid, SIGINT);
            }
        }
    }

    // Wait until all children have terminated
    printf("Waiting for all child processes to terminate...\n");
    while (numTerminated < numChildren) {
        sleep(1); // Sleep and rely on SIGCHLD handler to update count
    }

    printf("All child processes have terminated.\n");
    return 0;
}
