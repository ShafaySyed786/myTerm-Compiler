#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <pwd.h>
#include "mush.h"
#define PROMPT "8-P "

int batch_mode; // Declare batch_mode as a global variable
volatile sig_atomic_t got_sigint = 0;


void sigint_handler(int sig); // Declare the function prototype

void sigint_handler(int sig) {
    got_sigint = 1;
}

int main(int argc, char *argv[]){
    FILE *input;
    char* commandline;
    struct pipeline* peterpiper;
    int is_interactive = isatty(STDIN_FILENO); // Check if the input is from terminal
    batch_mode = (argc == 2); // True if we're reading commands from a file
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0; 
    sigemptyset(&sa.sa_mask);
    // Or SA_RESTART if you want to restart system calls on signal receipt
    // Block all signals during the execution of the signal handler

    if(sigaction(SIGINT, &sa, NULL) == -1){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }


    if(argc == 1){
        input = stdin;
    }
    else if(argc == 2){
        if(!(input = fopen(argv[1], "r"))) {
            perror("Input file couldn't be opened");
            exit(errno);
        }
    }
    else{
        fprintf(stderr, "usage: %s [ -v ] [ infile ]\n", argv[0]);
        exit(1);
    }

    while(!feof(input)){
        if(isatty(STDOUT_FILENO)) {
            printf(PROMPT); // Only print the prompt if the output is not being sent to a terminal
        }
        errno = 0;  // Clear errno
        commandline = readLongString(input);
        if (got_sigint) {
            got_sigint = 0;
            printf("\n");
            continue;
        }

        if((peterpiper = crack_pipeline(commandline)) == NULL){
            // Syntax error, print error message and continue to the next iteration
            continue;
        }
        
        // Check if the first command is "cd"
        struct clstage first_stage = peterpiper->stage[0];
        if(strcmp(first_stage.argv[0], "cd") == 0) {
            // Handle "cd" built-in command here
            if(first_stage.argc > 2) {
                fprintf(stderr, "usage: cd <directory>\n");
            } else if(first_stage.argc == 2) {
                if(chdir(first_stage.argv[1]) == -1) {
                    perror(first_stage.argv[1]);
                }
            } else {
                char *home_path;
                if((home_path = getenv("HOME")) || (home_path = getpwuid(getuid())->pw_dir)) {
                    if(chdir(home_path) == -1) {
                        perror("chdir error");
                        exit(errno);
                    }
                } else {
                    fprintf(stderr, "cd: HOME not set");
                    exit(EXIT_FAILURE);
                }
            }
            continue; // Go to the next loop iteration, printing the prompt at the start of the loop
        }
        
        int pipefd[2];
        int in = 0;
        int i;
        for(i = 0; i < peterpiper->length; i++) {
            struct clstage *current_stage = &(peterpiper->stage[i]);

            if(i != peterpiper->length - 1) {
                if(pipe(pipefd) < 0) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }
            }

            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGINT);
            if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
                perror("sigprocmask");
                exit(EXIT_FAILURE);
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                exit(EXIT_FAILURE);
            } else if (pid == 0) {
                // This is the child process
                if(i != 0) {
                    if(dup2(in, STDIN_FILENO) < 0) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                }

                if(i != peterpiper->length - 1) {
                    if(dup2(pipefd[1], STDOUT_FILENO) < 0) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                }

                if(current_stage->inname != NULL) {
                    int fd = open(current_stage->inname, O_RDONLY);
                    if(fd < 0) {
                        perror(current_stage->inname);
                        exit(EXIT_FAILURE);
                    }
                    if(dup2(fd, STDIN_FILENO) < 0) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    close(fd);
                }

                if(current_stage->outname != NULL) {
                    int fd = open(current_stage->outname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if(fd < 0) {
                        perror(current_stage->outname);
                        exit(EXIT_FAILURE);
                    }
                    if(dup2(fd, STDOUT_FILENO) < 0) {
                        perror("dup2");
                        exit(EXIT_FAILURE);
                    }
                    close(fd);
                }

                if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1) {
                    perror("sigprocmask");
                    exit(EXIT_FAILURE);
                }


                if(execvp(current_stage->argv[0], current_stage->argv) < 0) {
                    perror(current_stage->argv[0]);
                    exit(EXIT_FAILURE);
                }

            } else {
                // This is the parent process
                int status;
                waitpid(pid, &status, 0); // Wait for the specific child process to finish

                if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1) {
                    perror("sigprocmask");
                    exit(EXIT_FAILURE);
                }


                if(i != peterpiper->length - 1) {
                    close(pipefd[1]);
                    in = pipefd[0];
                }
            }
        }
    }
    return 0;
}
