#include "process.h"

// Directory node 
typedef struct DirNode{
    char* directory;
    struct DirNode* prev;
    struct DirNode* next;
} DirNode;
// Directory stack to push to or pop from
typedef struct DirStack{
    DirNode* head;
    DirNode* tail;
}DirStack;
// Empty stack
DirStack* directory_stack = NULL;

// Process pushd
int builtin_pushd(const CMD *cmdList){
    if (cmdList->argc != 2) {
        fprintf(stderr, "usage: pushd <directory-name>\n");
        return 1;
    }
    else {
        const char* targetDir = cmdList->argv[1];
        if(directory_stack == NULL) {
            // Initialize empty stack
            DirStack* new_stack = malloc(sizeof(DirStack));
            new_stack->head = NULL;
            new_stack->tail = NULL;
            directory_stack = new_stack;
        }
        // Get the current working directory
        char currentDir[PATH_MAX];
        if (!getcwd(currentDir, sizeof(currentDir))){
            perror("getcwd()");
            return (errno);
        }
        // Initialize node for this current directory
        DirNode* new_directory = malloc(sizeof(DirNode));
        char* dirName = malloc(sizeof(char) * strlen(currentDir));
        strcpy(dirName, currentDir);
        new_directory->directory = dirName;
        new_directory->next = NULL;
        new_directory->prev = NULL;
        // Push current directory to directory stack
        if(directory_stack->head != NULL) {
            directory_stack->head->next = new_directory;
            new_directory->prev = directory_stack->head;
            directory_stack->head = new_directory;
        }
        else {
            directory_stack->head = new_directory;
            directory_stack->tail = new_directory;
        }
        // Go to target directory (provided as arg)
        if (chdir(targetDir) == -1){
            perror("chdir");
            return errno;
        }
        // Setenv
        setenv("PWD", targetDir, 1);
        // Print directory stack, starting with cwd
        DirNode* node = directory_stack->head;
        char curr_dir_print[PATH_MAX];
        if (!getcwd(curr_dir_print, PATH_MAX)){
            perror("getcwd");
            return (errno);
        }
        fprintf(stdout, "%s", curr_dir_print);
        while (node != NULL){
            fprintf(stdout, " %s", node->directory);
            node = node->prev;
        }
        fprintf(stdout, "\n");
        return 0;  // success
    }
}

// Process popd
int builtin_popd(const CMD *cmdList){
    if (directory_stack->head == NULL){
        fprintf(stderr, "popd: Empty directory stack\n");
        return 1;
    }

    DirNode* prev_top = directory_stack->head;
    // one item on stack
    if (prev_top->prev == NULL)
    {
        directory_stack->head = NULL;
        directory_stack->tail = NULL;
    }
    else
    {
        // pop and reset top
        DirNode* curr_top = directory_stack->head->prev;
        directory_stack->head = curr_top;
    }
    if (chdir(prev_top->directory) == -1){
        perror("chdir");
        return (errno);
    }
    // set env
    setenv("PWD", prev_top->directory, 1);
    DirNode* node = directory_stack->head;
    char curr_dir_print[PATH_MAX];
    if (!getcwd(curr_dir_print, PATH_MAX)){
        perror("getcwd");
        return (errno);
    }
    fprintf(stdout, "%s", curr_dir_print);
    while (node != NULL){
        fprintf(stdout, " %s", node->directory);
        node = node->prev;
    }
    fprintf(stdout, "\n");
    
    // Free 
    free(prev_top->directory);
    free(prev_top);
    return 0;
}

// Process cd 
int builtin_cd(const CMD *cmdList) {
    const char *path;
    char new_pwd[1024];

    if (cmdList->argc == 1) {
        path = getenv("HOME");
    } else if (cmdList->argc == 2) {
        path = cmdList->argv[1];
    } else {
        fprintf(stderr, "usage: cd OR cd <directory-name>\n");
        return 1;
    }
    if (chdir(path) != 0) {
        perror("cd");
        return (errno); // Return non-zero if there's an error
    }
    // Get the new present working directory
    if (getcwd(new_pwd, sizeof(new_pwd)) == NULL) {
        perror("getcwd");
        return (errno);
    }
    // Set the new PWD environment variable
    setenv("PWD", new_pwd, 1);
    return 0;
}

// Processs redirections
int process_redir(const CMD *cmdList){
    //int toType;           // Redirect stdout: NONE (default), RED_OUT (>),
                        //   RED_OUT_APP (>>), or  RED_OUT_ERR (&>)

    // order of flags doesn't matter
    // Handle output redirection '>'
    if (cmdList->toType == RED_OUT) {
        int out_fd = open(cmdList->toFile, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
        if (out_fd == -1) {
            perror("open");
            exit(errno);
        }
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }
    // Handle appending redirection '>>'
    if (cmdList->toType == RED_OUT_APP) {
        int append_fd = open(cmdList->toFile, O_WRONLY | O_CREAT | O_APPEND, S_IRWXU);
        if (append_fd == -1) {
            perror("open");
            exit(errno);
        }
        dup2(append_fd, STDOUT_FILENO);
        close(append_fd);
    }
    //int fromType;         // Redirect stdin: NONE (default), RED_IN (<), or
                        //   RED_IN_HERE (<<)
    //char *fromFile;       // File to redirect stdin, contents of here document,
                        //   or NULL (default)
    // Handle input redirection '<'
    if (cmdList->fromType == RED_IN) {
        int in_fd = open(cmdList->fromFile, O_RDONLY);
        if (in_fd == -1) {
            perror("open");
            exit(errno);  
        }
        dup2(in_fd, STDIN_FILENO);
        close(in_fd); 

    }
    // Handle heredoc redirection '<<'
    if (cmdList->fromType == RED_IN_HERE) {
        char template[] = "/tmp/fileXXXXXX";  // Temporary file template for mkstemp GLOBAL???
        int temp_fd;
        // Create a temporary file using mkstemp
        if ((temp_fd = mkstemp(template)) == -1) {
            perror("mkstemp");
            exit(errno);
        }
        // Write the contents of the heredoc to it
        if (write(temp_fd, cmdList->fromFile, strlen(cmdList->fromFile)) == -1) {
            perror("write");
            exit(errno);
        }
        // Reposition the file cursor to the start of the file
        if (lseek(temp_fd, 0, SEEK_SET) == -1) {
            perror("lseek");
            exit(errno);
        }
        // Use dup2 to overwrite stdin fileno
        dup2(temp_fd, STDIN_FILENO);
        // close() non stdin, stdout, stderr fds.
        close(temp_fd);
        unlink(template); 
    }
    return 0;
}
// Processs simple commands
int process_simple_cmd(const CMD *cmdList){
    int status, execution_res;

    // Handle builtin commands 
    if (strcmp(cmdList->argv[0], "cd") == 0){
        return builtin_cd(cmdList);
    }
    else if (strcmp(cmdList->argv[0], "pushd") == 0){
        return builtin_pushd(cmdList);
    }
    else if (strcmp(cmdList->argv[0], "popd") == 0){
        return builtin_popd(cmdList);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(errno);
    }
    else if (pid == 0) {  // Child process
        // Handle local variable assignments
        for (int i = 0; i < cmdList->nLocal; i++) {
            setenv(cmdList->locVar[i], cmdList->locVal[i], 1);
        }
        // Handle redirection
        status = process_redir(cmdList);
        //execute command
        // If execvp fails
        if ((execvp(cmdList->argv[0], cmdList->argv)) == -1){
            perror("execvp");
            exit(errno);
        }
    }
    else {  // Parent
        waitpid(pid, &status, 0);
        execution_res = STATUS(status);
    }
    // Set exit status in environment variable
    char exit_string[10];
    sprintf(exit_string, "%d", execution_res);
    setenv("?", exit_string, 1);
    
    return execution_res;
}

// Process pipe
int process_pipeline(const CMD *cmdList) {
    int right_status, left_status, final_exit_status;
    int pipefds[2];
    pid_t left_pid, right_pid;

    // Create pipe
    if (pipe(pipefds) == -1) {
        perror("pipe");
        exit(errno);
    }

    // Fork for the left side of the pipe
    if ((left_pid = fork()) == -1) {
        perror("fork");
        exit(errno);
    }

    if (left_pid == 0) {  // In left child
        // Close the read end of the pipe, we're going to write to it.
        close(pipefds[0]);

        // Redirect stdout to the write end of the pipe
        dup2(pipefds[1], STDOUT_FILENO);
        close(pipefds[1]);

        // Execute the left command recursively
        //handle_simple(left_cmd);
        exit(process(cmdList->left));
    }

    // Fork for the right side of the pipe
    if ((right_pid = fork()) == -1) {
        perror("fork");
        exit(errno);
    }

    if (right_pid == 0) {  // In right child
        // Close the write end of the pipe, we're going to read from it.
        close(pipefds[1]);

        // Redirect stdin to the read end of the pipe
        dup2(pipefds[0], STDIN_FILENO);
        close(pipefds[0]);

        // Execute the right command
        exit(process(cmdList->right));
    }

    // Parent process
    close(pipefds[0]);
    close(pipefds[1]);

    // Wait for the rightmost child process to complete
    waitpid(right_pid, &right_status, 0);
    int right_exit_status = STATUS(right_status);

    // Wait for the left child process to complete
    waitpid(left_pid, &left_status, 0);
    int left_exit_status = STATUS(left_status);

    if (right_status != 0) {
        final_exit_status = right_exit_status;
    } else {
        final_exit_status = left_exit_status;
    }
    // Convert the exit status to a string
    char exit_str[10];
    sprintf(exit_str, "%d", final_exit_status);
    // Set $? to the exit status
    setenv("?", exit_str, 1);
    return final_exit_status;
}

// Process the 'AND' conditional.
int process_and(const CMD *cmdList) {
    int left_status = process(cmdList->left);
    // If the left command succeeds (returns 0), execute and return the status of the right command
    if (left_status == 0) {
        return process(cmdList->right);
    } 
    // If the left command fails (non-zero return value), return its status without executing the right command
    else {
        return left_status;
    }
}

// Process the 'OR' conditional.
int process_or(const CMD *cmdList) {
    int left_status = process(cmdList->left);
    // If the left command fails (non-zero return value), execute and return the status of the right command
    if (left_status != 0) {
        return process(cmdList->right);
    } 
    // If the left command succeeds (returns 0), return its status without executing the right command
    else {
        return left_status;
    }
}

// Process subcommand
int process_subcommand(const CMD *cmdList) {
    int status;
    int exit_status;
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0) { // Child
        // Handle any redirections associated with the subcommand
        if (process_redir(cmdList) != 0) {
            perror("process_redir");
            exit(errno);  // If redirection failed, exit with failure status
        }
        exit(process(cmdList->left));
    } else { // Parent
        waitpid(pid, &status, 0);
        exit_status = STATUS(status);

        char str[20]; // Buffer to hold the string representation of the status
        sprintf(str, "%d", exit_status); // Convert the status to string
        setenv("?", str, 1); // Set env var
    
    }
    return exit_status;
}
// Process sep end and bg : TO DO
int process_end_bg(const CMD *cmdList, bool runInBackground) {
    int returnVal; 
    pid_t pid;
    // handle sep_end
    if (cmdList->type == SEP_END) {
        if (cmdList->left->type == SEP_BG || cmdList->left->type == SEP_END) {
            process_end_bg(cmdList->left, false);
        } else {
            returnVal = process(cmdList->left);
        } 
    // handle bg    
    } else if (cmdList->type == SEP_BG) {
        if (cmdList->left->type == SEP_BG || cmdList->left->type == SEP_END) {
            process_end_bg(cmdList->left, true);
        } else {
            pid = fork();
            if (pid < 0) {
                perror("fork()");
                exit(errno);
            } else if (pid == 0) { // Child process
                exit(process(cmdList->left));
            } else { // Parent
                fprintf(stderr, "Backgrounded: %d\n", pid);
            }
        }
    }
    // run cmd in bg based on bool flag
    if (runInBackground) {
        if (fork()< 0) {
            perror("fork()");
            exit(errno);
        } else if (pid == 0) { // Child
            exit(process(cmdList->right));
        } else { 
            // Parent
            fprintf(stderr, "Backgrounded: %d\n", pid);
        } 
    }
    else {
        returnVal = process(cmdList->right);
    }
    return returnVal;
}

// Reap zombies
void reap_zombies(int *status) {
    pid_t pid;
    // Use waitpid with WNOHANG to non-blockingly reap zombies
    while ((pid = waitpid(-1, status, WNOHANG)) > 0) {
        fprintf(stderr, "Completed: %d (%d)\n", pid, *status);
    }
}

// Main process function: ties everything together
int process(const CMD *cmdList) {
    int returnVal;
    if (cmdList->type == SIMPLE) {
        returnVal = process_simple_cmd(cmdList);
    } else if (cmdList->type == PIPE) {
        returnVal = process_pipeline(cmdList);
    } else if (cmdList->type == SEP_AND) {
        returnVal = process_and(cmdList);
    } else if (cmdList->type == SEP_OR) {
        returnVal = process_or(cmdList);
    } else if (cmdList->type == SEP_BG || cmdList->type == SEP_END) {
        returnVal = process_end_bg(cmdList, 0);
    } else if (cmdList->type == SUBCMD) {
        returnVal= process_subcommand(cmdList);
    } else {
        // Handle any other types or potential error cases here
        returnVal = -1;
    }
    int status;
    reap_zombies(&status);
    return returnVal;
}