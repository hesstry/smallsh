#define _GNU_SOURCE
#include <stdlib.h>
#define _GNU_SOURCE
#include <unistd.h>
#define _GNU_SOURCE
#include <stdio.h>
#define _GNU_SOURCE
#include <time.h>
#define _GNU_SOURCE
#include <sys/wait.h>
#define _GNU_SOURCE
#include <string.h>
#define _GNU_SOURCE
#include <sys/stat.h>
#define _GNU_SOURCE
#include <fcntl.h>
#define _GNU_SOURCE
#include <signal.h>

#define MAX_ARGS 512
#define MAX_CHARS 2048

// int val to determine if the process is in foreground only mode
int foreground_mode = 0;
// pid_t to determine what the last foreground process was, this is to ensure the print statement for notifying the user of foreground only mode isn't called until back to smallsh proc
pid_t last_foreground_proc;
// TODO does not account for multiple background processes
// pid_t to determine the background proc that ends while smallsh is running
pid_t background_proc_ending_pid = -2;
// did the background proc exit or terminate due to a signal?
int background_proc_end_normal = -2;
// exit value or termination signal
int background_proc_end_signal = -2;
// int val to keep track of all currently running children
int n_children = 0;

// code taken from a stackoverflow because brain hurts, commented to show I understand what the code is doing and not using it blindly
// this is used to create a reentrant function to use in a signal handler
// all it does is write the int to screen with write
// https://stackoverflow.com/questions/4629050/convert-an-int-to-ascii-character
void intToASCII(int num)
/* 
    input:
        int num: number to convert to string

    returns:
        none

    functionality:
        reentrant function to convert a number to a string and print to screen
 */
{
    // create a tmp num to first calculate number of digits
    int temp = num;
    int num_digits = 0;

    // if the number is 0 then we want to ensure the num digits corrsponds to this
    if (temp == 0)
    {
        num_digits = 1;
    }

    // calculate number of digits
    while (temp != 0)
    {
        temp = temp / 10; // each time we can divide by 10, the highest factor of ten grows by one, this determines the number of digits
        num_digits++;
    }

    // create the string to record the string version of the number
    char buf[num_digits+1];

    // perform modulus division to get the remainder of each factor of 10, starting with the ones and updating buf accordingly
    for (int i = num_digits-1; i >= 0; i--)
    {
        buf[i] = (char)((num%10) + 48);
        // integer division to calculate the 0-9 coefficient of the next factor of ten
        num = num / 10;
    }
    // append null char to end of string
    buf[num_digits] = '\0';
    // write to screen
    write(1, buf, num_digits);
    fflush(stdout);
    return;
}

void handle_SIGCHLD(int signum)
/* 
    input: 
        int signum: this is the signal number of the signal handler

    returns:
        none

    functionality:
        this function handles the termination of background child processes and updates values in the parent proc
 */
{
    pid_t spawnpid;
    int childStatus;
    int exit_value;
    // wait pid with pid=-1 means wait for any child, because this is executed by a SIGCHLD
    // signal, it will pick up the pid of the terminated child
    // WNOHANG used to continue execution if no child is terminated
    spawnpid = waitpid(-1, &childStatus, WNOHANG);
    // we don't want this called if no children are being waited for
    // a spawnpid means a null change in children, a process that terminates immediately will send a SIGCHLD signal,
    // but unless it was a background process, it has already been handled, both success and failure
    if (spawnpid != 0 && (spawnpid != -1))
    {
        if(WIFEXITED(childStatus))
        {
            background_proc_ending_pid = spawnpid;
            background_proc_end_normal = 1;
            background_proc_end_signal = WEXITSTATUS(childStatus);
            // printf("background pid %d is done: exit value %d\n", background_proc_ending_pid, background_proc_end_signal);
            write(1, "background pid ", 16);
            fflush(stdout);
            intToASCII(background_proc_ending_pid);
            write(1, " is done: exit value ", 22);
            fflush(stdout);
            intToASCII(background_proc_end_signal);
            write(1, "\n: ", 3);
            fflush(stdout);
        }
        else
        {
            // printf("background pid %d is done: terminated by signal %d\n", background_proc_ending_pid, background_proc_end_signal);
            background_proc_ending_pid = spawnpid;
            background_proc_end_normal = 0;
            background_proc_end_signal = WTERMSIG(childStatus);
            write(1, "\nbackground pid ", 17);
            fflush(stdout);
            intToASCII(background_proc_ending_pid);
            write(1, " is done: terminated by signal ", 32);
            fflush(stdout);
            intToASCII(background_proc_end_signal);
            fflush(stdout);
            write(1, "\n: ", 3);
            fflush(stdout);
        }
    }
}

// the handle for SIGINT in the child process will exit upon receiving this signal
void handle_SIGINT_child(int signum)
/* 
    input:
        int signum: the signal number

    returns:
        none

    functionality:
        anytime SIGINT is called within a child process, the child terminates itself with the kill function
 */
{
    pid_t pid = getpid();
    kill(pid, SIGINT);
}

void handle_SIGTSTP_parent(int signum)
/* 
    input:
        int signum: the signal number

    returns:
        none

    functionality:
        causes parent to handle SIGTSTP's by activating foreground only mode, updating the global variable foreground_mode and writing to the screen
 */
{
    foreground_mode = (foreground_mode == 0) ? 1 : 0;
    int childStatus;
    int childPid;
    // perform a blocking wait so that the write statement isn't called until the waitpid is returned
    childPid = waitpid(last_foreground_proc, &childStatus, 0);
    if (foreground_mode == 1)
    {
        write(1, "\nEntering foreground-only mode (& is now ignored)\n: ", 52);
        fflush(stdout);
    }
    else
    {
        write(1, "\nExiting foreground-only mode\n: ", 33);
        fflush(stdout);
    }
}

// code taken from https://stackoverflow.com/questions/12427524/killing-child-processes-at-parent-process-exit
// commented to show I understand what is going on and not blindly copy pasting code from internets
void handle_SIGUSR1()
/* 
    input:
        none

    returns:
        none

    functionality:
        perform a loop to ensure all children have been cleaned up upon exit and no zombie procs exist afterwards
 */
{
    // kill with pid = 0 will send kill to all procs in calling proc process group, children inherit proc group id
    kill(0, SIGUSR2);
    // keep calling wait until all children have been cleaned up
    // wait does the clean up from the proc table
    while (n_children > 0) {
        // the waitstatus of the child proc we dont care to store so NULL pointer is fine, throw that baby into the black hole (this is dark sounding stuff outside of context)
        if (wait(NULL) != -1) {
            n_children--;
        }
    }
}

void handle_SIGUSR2(int signum)
/* 
    input:
        int signum: signal number of signal handler

    returns:
        none

    functionality:
        once received a SIGUSR2 signal, exits out of the child process, therefore sending a signal to the waiting parent process that it has terminated
 */
{
    /* pid_t pid = getpid();
    kill(pid, SIGTERM); */
    exit(0);
}

/* 
    this structure keeps track of each commands arguments and attributes

    command is to be executed in the child process

    argc is the argument count of the command

    in_redirect_bool tells whether input redirection is involved in the command

    in_redirect is the new file path to redirect stdin

    out_redirect_bool tells whether output redirection is involved in the command

    out_redirect is the new file path to redirect stdout

    bg is a boolean value telling whether or not a command is to be ran as a background process

    args is an array of strings holding the required structure asked by execvp commands
 */
struct command{
    char *command;
    int *argc;
    int *in_redirect_bool;
    char *in_redirect;
    int *out_redirect_bool;
    char *out_redirect;
    int *bg;
    char **args;
};

// initialize a globalCommand to NULL, it will be updated and freed throughout the program execution
struct command *globalCommand=NULL;

struct command *createCommand(char *input, int argc, int bg)
/* 
    input:
        char *input: the input string given at smallsh prompt
        int argc: the argument count of the given command
        int bg: boolean value for running command as background proc

    returns:
        struct command *: a command structure containing updated attributes

    functionality:
        handles the prompt input given by the user and creates a dynamically allocated command struct with dynamically allocated attributes
 */
{
    // boolean values relating to whether or not a redirection, input or output, is being processed
    int in_redirect = 0;
    int out_redirect = 0;
    struct command *currCommand = malloc(sizeof(struct command));

    // create NULL pointers so freeing doesn't lead to undefined behaviour
    currCommand->in_redirect = NULL;
    currCommand->out_redirect = NULL;

    // create and initialize value for bg
    currCommand->bg = malloc(sizeof(int));
    *(currCommand->bg) = bg;

    // allocate mem for both bool values and initialize them
    currCommand->in_redirect_bool = malloc(sizeof(int));
    *(currCommand->in_redirect_bool) = 0;
    currCommand->out_redirect_bool = malloc(sizeof(int));
    *(currCommand->out_redirect_bool) = 0;

    // initialize the boolean values for the commands redirection attributes
    // allocate mem for argc and initialize it to counted args
    currCommand->argc = malloc(sizeof(int));
    *(currCommand->argc) = argc;
    // initialize the args array as an empty array of string pointers
    // ensure there is room for the command itself at the start, and a NULL pointer at the end, argc+2
    currCommand->args = malloc((argc+2)*sizeof(char*));

    // when using strtok_r, we must have a saveptr so that we can linearily process multiple strings at once
    char *saveptr;
    // processed keeps track of the command and args currently processed, should end as argc
    int processed = 0;
    int argsIndex = 0;

    // token code adapted from exploration 2
    char *token;
    // first process the command
    token = (char*)strtok_r(input, " ", &saveptr); // input string, delimeter, saveptr to be updated after strtok_r executed
    currCommand->command = calloc(strlen(token)+1,sizeof(char));
    strcpy(currCommand->command, token);
    // also ensure the command is copied so that we have the format needed when running execvp
    currCommand->args[argsIndex] = calloc(strlen(token)+1, sizeof(char));
    strcpy(currCommand->args[argsIndex], token);
    argsIndex++;
    // handle echo differently, since we want it to just print out all the spaces of a string, and using token the same on this as argument syntax goes for other commands deletes the spaces
    if (strncmp(token, "echo", 4)==0)
    {
        // saveptr starts right after the first space, since space is the delimeter, and we want all text after that first space
        currCommand->args[argsIndex] = calloc(strlen(saveptr)+1, sizeof(char));
        strcpy(currCommand->args[argsIndex], saveptr);
        argsIndex++;
    }
    else
    {
        while ((token = (char*)strtok_r(NULL, " ", &saveptr)) != NULL)
        {
            // process input and output recognition
            // want to make sure this comes first to activate processing of io redirection, so that <, > and io files are not processed as args
            if (strncmp(token, "< ", 1)==0)
            {
                in_redirect = 1;
            }
            else if (strncmp(token, "> ", 1)==0)
            {
                out_redirect = 1;
            }
            // if we are in the processing of redirection, ensure we've reached the io file to use as redirection and are not still on the < or > symbols
            if (in_redirect != 0 && (strncmp(token, "< ", 1)!=0))
            {
                // update boolean value for input redirection
                currCommand->in_redirect = calloc(strlen(token)+1, sizeof(char));
                strcpy(currCommand->in_redirect, token);
                *(currCommand->in_redirect_bool) = 1;
                in_redirect = 0;
            }
            else if (out_redirect != 0 && (strncmp(token, "> ", 1)!=0))
            {
                // update boolean value for output redirection
                currCommand->out_redirect = calloc(strlen(token)+1, sizeof(char));
                strcpy(currCommand->out_redirect, token);
                *(currCommand->out_redirect_bool) = 1;
                out_redirect = 0;
            }
            // only add arguments if they are actually arguments, redirection means no arguments are being processed
            // also don't process the ampersand as an argument or it will fail the exec call
            else if ((in_redirect == 0) && (out_redirect == 0) && (strncmp(token, "& ", 1) != 0))
            {
/*                 printf("Processing argument.\n");
                fflush(stdout); */
                // allocate space for the arg to be added to the array of args
                currCommand->args[argsIndex] = calloc(strlen(token)+2, sizeof(char));
                // copy token into newly allocated space
                strcpy(currCommand->args[argsIndex], token);
                processed++;
                argsIndex++;
            }
        }
    }
    currCommand->args[argsIndex] = NULL;

    return currCommand;
}

char *varExpansion(char *input, int length)
/* 
    input:
        char *input: the input processed from smallsh prompt
        int length: the length of the input

    returns:
        char *: a variable expanded string

    functionality:
        processes input to expand all $$ occurrences to the pid of the smallsh proc
 */
{
    int pid = getpid();
    char charPid[10];
    // create a string version of PID, save length of PID in pidLen
    int pidLen = sprintf(charPid, "%d", pid);
    int charCounter = 1;
    int newLength = 0;

    while (charCounter-1 < length)
    {
        if (input[charCounter-1]=='$')
        {
            if ((charCounter < length) && (input[charCounter]=='$'))
            {
                newLength = newLength + pidLen;
                // leap character counter + 2 so that we don't recount the $'s to substitute
                charCounter = charCounter + 2;
            }
        } 
        // if here, then we didn't process a double $, and we just copy over the left char of the pair being checked
        else
        {
            newLength++;
            // increment charCounter by 1 to check next pair of chars
            charCounter++;
        }
    }
    // if reached here, then we've finished processing the string for the new length of substitutions requirements
    // initialize new string
    char *substituted = calloc((newLength+1), sizeof(char));
    int oldInputCounter = 1;
    int newInputCounter = 0;
    // this while loop now copies over the string and accomodates substitutions
    while (oldInputCounter-1 < length)
        {
            if (input[oldInputCounter-1]=='$')
            {
                if ((oldInputCounter < length) && (input[oldInputCounter]=='$'))
                {
                    // add all chars in the charPid string to substituted, and increment charCounter each time so we keep the correct charCounter
                    for (int i = 0; i < pidLen; i++)
                    {
                        substituted[newInputCounter] = charPid[i];
                        newInputCounter++;
                    }
                    // make sure we point oldInputCounter to first char after the double $
                    oldInputCounter = oldInputCounter + 2;
                }
            } 
            // if here, then we didn't process a double $, and we just copy over the left char of the pair being checked
            else
            {
                substituted[newInputCounter] = input[oldInputCounter-1];
                newInputCounter++;
                oldInputCounter++;
            }
        }
    return substituted;
}

struct command *prompt()
/* 
    input:
        none

    returns:
        struct command *: returns a command struct with processed input

    functionality:
        prompts user for input and calls varExpansion and createCommand before returning a finalized struct command *
 */
{
    printf(": ");
    fflush(stdout);
    char input[MAX_CHARS];
    memset(input, '\0', MAX_CHARS);
    fgets(input, MAX_CHARS+1, stdin);
    int input_len = strlen(input);
    int bg = 0;

    if (input_len > MAX_CHARS)
    {
        return NULL;
    }

    int argc=0;
    int input_counter = 0;
    int redirection_count = 0;

    // count the number of args, number of args = number of spaces minus 2*(redirect count), command does not count as an arg, and redirections will be counted 1 for < or >, and 1 for the redirection input or output
    // echo is just a print of the following text, meaning it takes one argument
    if (strncmp(input, "echo", 4) == 0)
    {
        argc = 1;
    }
    else
    {
        while (input_counter < input_len)
        {
            // below conditional allows extra spaces to be used, to mimic the bash shell more so, this probably won't be tested
            // if there is a space and the next char is also a space, don't count arguments
            if ((input[input_counter] == ' ') && (input_counter < input_len-1) && (input[input_counter+1] != '\n') && (input[input_counter+1] != ' ')) 
            {
                argc++;
            }
            // i want to ensure redirections are not counted as arguments, only the command and its arguments are accounted for
            if (input[input_counter] == '<' || input[input_counter] == '>')
            {
                redirection_count++;
            }
            input_counter++;
        }
    }
    argc = argc - 2*redirection_count;
    if (argc > MAX_ARGS)
    {
        return NULL;
    }

    if (input_len == 1 || input[0] == '#')
    {
        return NULL;
    }
    // get rid of the newline char from the input
    input[input_len-1] = '\0';

    // lastly, check to see if the last character is an ampersands
    // we want to make sure ' &\0' is the final char if bg proc is to be ran, & @ index input_len-2, the space @index input_len-3
    if ((input_len - 2 > 0) && (input_len - 3 > 0) && (input[input_len-2]=='&') && (input[input_len-3]==' '))
    {
        // only activate bg mode if foreground_only_mode is set to OFF
        if (foreground_mode == 0)
        {
            bg = 1;
        }
        // decrement argc by one since this would have been counted as an argument
        argc--;
    }

    // take input and perform variable expansion if needed
    char *vars_expanded = varExpansion(input, strlen(input));
    struct command *newCommand = createCommand(vars_expanded, argc, bg);
    // free the expanded string created while checking for substitutions
    free(vars_expanded);
    return newCommand;
}

void cd(struct command *command)
/* 
    input:
        struct command *command: command struct pointer with possible arguments

    returns:
        none

    functionality:
        calls chdir() to evoke a cd command and takes a filepath, relative or absolute, as an argument
 */
{
    char buf[256];
    int success;
    if (*command->argc == 0 || (*command->argc > 0 && strlen(command->args[1])==0))
    {
        success = chdir(getenv("HOME"));
    }
    else
    {
        success = chdir(command->args[1]);
    }

    if (success != 0)
    {
        printf("Directory not found, path change failed.\n");
        fflush(stdout);
    }
}

void status(int statusVal)
/* 
    input:
        int statusVal: the status val to be printed to the screen

    returns:
        none

    functionality:
        simple print function for the statusVal variable with specified format
 */
{
    printf("exit value %d\n", statusVal);
    fflush(stdout);
}

int otherCommands(struct command *currCommand)
/* 
    input:
        struct command *currCommand: command struct pointer to process commands with possible arguments

    returns:
        int: an int indicating whether the command was executed correctly 

    functionality:
        calls exec function with the currCommand->args list to attempt to execute the command. returns a value and print statement indicating success or failure.
 */
{
    int childStatus;
    int childPid;
    pid_t spawnpid = fork();
    last_foreground_proc = spawnpid;

    if (spawnpid == -1)
    {
        printf("Fork failed.\n");
        fflush(stdout);
        exit(1);
    }
    else if (spawnpid == 0)
    { 
        // ensure a child proc is unaffected by SIGTSTP
        struct sigaction SIGTSTP_action = {0};
        SIGTSTP_action.sa_handler = SIG_IGN;
        sigfillset(&SIGTSTP_action.sa_mask);
        SIGTSTP_action.sa_flags = 0;
        sigaction(SIGTSTP, &SIGTSTP_action, NULL);

        // tell children to exit when they receive this signal upon parent exit
        struct sigaction SIGUSR2_action = {0};
        SIGUSR2_action.sa_handler = handle_SIGUSR2;
        sigfillset(&SIGUSR2_action.sa_mask);
        SIGUSR2_action.sa_flags = 0;
        sigaction(SIGUSR2, &SIGUSR2_action, NULL);

        struct sigaction SIGINT_action = {0};
        SIGINT_action.sa_handler = handle_SIGINT_child;
        // add all signals to block/ignore while SIGINT signal is being processed
        sigfillset(&SIGINT_action.sa_mask);
        // set flags to 0, no flags used
        SIGINT_action.sa_flags = 0;
        // install signal handler using sigaction function
        sigaction(SIGINT, &SIGINT_action, NULL);
        execvp(currCommand->command, currCommand->args);
        exit(1);
    }
    else
    {
        if (*(currCommand->bg) != 0)
        {
            printf("background pid is %d\n", spawnpid);
            fflush(stdout);
            spawnpid = waitpid(spawnpid, &childStatus, WNOHANG);
            // if we are here, we've successfully created a child process in the background
            n_children++;
        }
        else
        {
            spawnpid = waitpid(spawnpid, &childStatus, 0);
        }
		if(WIFEXITED(childStatus)){
            if (WEXITSTATUS(childStatus) == 1)
            {
                printf("%s: no such file or directory\n", currCommand->command);
                fflush(stdout);
                return 1;
            }
            else
            {
                return 0;
            }
		} else{
            printf("terminated by signal %d\n", WTERMSIG(childStatus));
            fflush(stdout);
            return 1;
		}
    }
}

int io_redirection(struct command *currCommand)
/* 
    input:
        struct command *currCommand: command struct to process

    returns:
        int: value indicating success or failure

    functionality:
        similar to otherCommands, except this is called when IO redirection is involved in a command, redirecting as the command asks. indicates with print statements and return value
        the success of attempted redirection and command execution.
 */
{
    int childStatus;
    int childPid;
    pid_t spawnpid = fork();

    // if were not running a background proc, then save this proc as the last foreground proc
    if (*(currCommand->bg)==0)
    {
        last_foreground_proc = spawnpid;
    }

    if (spawnpid == -1)
    {
        printf("Fork failed.\n");
        fflush(stdout);
        exit(1);
    }
    else if (spawnpid == 0)
    {
        // ensure a child proc is unaffected by SIGTSTP
        struct sigaction SIGTSTP_action = {0};
        SIGTSTP_action.sa_handler = SIG_IGN;
        sigfillset(&SIGTSTP_action.sa_mask);
        SIGTSTP_action.sa_flags = 0;
        sigaction(SIGTSTP, &SIGTSTP_action, NULL);

        // tell children to exit when they receive this signal upon parent exit
        struct sigaction SIGUSR2_action = {0};
        SIGUSR2_action.sa_handler = handle_SIGUSR2;
        sigfillset(&SIGUSR2_action.sa_mask);
        SIGUSR2_action.sa_flags = 0;
        sigaction(SIGUSR2, &SIGUSR2_action, NULL);

        // begin redirection
        char *openBuf0 = "file open failure input\n";
        char *openBuf1 = "file open failure output\n";
        char *dupBuf = "dup() failure\n";
        int result;
        if (*(currCommand->in_redirect_bool) == 1)
        {
            int sourceFD = open(currCommand->in_redirect, O_RDONLY);
            if (sourceFD == -1) { 
                char sourceBuf[100];
                int buf_len = sprintf(sourceBuf, "cannot open %s for input redirection\n", currCommand->in_redirect);
                write(1, sourceBuf, buf_len+1);
                // write(1, openBuf0, 18);
                fflush(stdout);
                exit(1); 
            }
            result = dup2(sourceFD, 0);
            if (result == -1) { 
                write(1, dupBuf, 15);
                fflush(stdout);
                exit(1);
            }
        }
        // if the input redirection isn't stated, but we are running a background process, then redirect stdin to a black hole
        else if (*(currCommand->bg) == 1)
        {
            int sourceFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (sourceFD == -1) { 
                char *sourceBuf = "cannot open /dev/null for input redirection\n";
                write(1, sourceBuf, 45);
                fflush(stdout);
                exit(1); 
            }
            result = dup2(sourceFD, 0);
            if (result == -1) { 
                write(1, dupBuf, 12);
                fflush(stdout);
                exit(1);
            }
        }
        if (*(currCommand->out_redirect_bool) == 1)
        {
            int destFD = open(currCommand->out_redirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (destFD == -1) { 
                write(1, openBuf1, 19);
                fflush(stdout); 
                exit(1); 
            }
            result = dup2(destFD, 1);
            if (result == -1) { 
                write(1, dupBuf, 12);
                fflush(stdout);
                exit(1);
            }
        }
        // if the output redirection isn't stated, but we are running a background process, then redirect stdout to a black hole
        else if (*(currCommand->bg) == 1)
        {
            int destFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (destFD == -1) { 
                write(1, openBuf1, 19);
                fflush(stdout); 
                exit(1); 
            }
            result = dup2(destFD, 1);
            if (result == -1) { 
                write(1, dupBuf, 12);
                fflush(stdout);
                exit(1);
            }
        }
        execvp(currCommand->command, currCommand->args);
        exit(1);
    }
    else
    {
        if (*(currCommand->bg) != 0)
        {
            printf("background pid is %d\n", spawnpid);
            fflush(stdout);
            spawnpid = waitpid(spawnpid, &childStatus, WNOHANG);
            // if we are here, we've successfully created a child process in the background
            n_children++;
        }
        else
        {
            spawnpid = waitpid(spawnpid, &childStatus, 0);
        }
		if(WIFEXITED(childStatus)){
            if (WEXITSTATUS(childStatus) != 0)
            {
                return 1;
            }
            else
            {
                return 0;
            }
		} else{
			printf("smallsh: %s: command not found\n", currCommand->command);
            fflush(stdout);
            return 1;
		}
    }
}

void freeGlobalCommand(struct command *command)
/* 
    input:
        struct command *command: command to be freed

    returns:
        none

    functionality:
        free function for each created command    
 */
{
    int i = 0;
    // free all arguments within the command first
    while (i < *(globalCommand->argc)+2)
    {
        free(globalCommand->args[i]);
        i++;
    }
    free(globalCommand->args);
    free(globalCommand->command);
    free(globalCommand->argc);
    free(globalCommand->in_redirect);
    free(globalCommand->in_redirect_bool);
    free(globalCommand->out_redirect);
    free(globalCommand->out_redirect_bool);
    free(globalCommand->bg);
    free(globalCommand);
}

void main(){
    last_foreground_proc = getpid();
    // set up signal handling for SIGCHLD to process automatically, termination of bg child procs
    // initialize empty sigaction
    struct sigaction SIGCHLD_action = {0};
    // assign SIGCHLD handler to the sigaction struct
    SIGCHLD_action.sa_handler = handle_SIGCHLD;
    // add all signals to block/ignore while SIGCHLD signal is being processed
    sigfillset(&SIGCHLD_action.sa_mask);
    // set flags to 0, no flags used
    SIGCHLD_action.sa_flags = SA_RESTART;
    // install signal handler using sigaction function
    sigaction(SIGCHLD, &SIGCHLD_action, NULL);

    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    // add all signals to block/ignore while SIGCHLD signal is being processed
    sigfillset(&SIGINT_action.sa_mask);
    // set flags to 0, no flags used
    SIGINT_action.sa_flags = 0;
    // install signal handler using sigaction function
    sigaction(SIGINT, &SIGINT_action, NULL);

    // specified TSTP signal handler
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP_parent;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // lastly, handle kill sig to perform cleanup at end of program SIGUSR1_action
    struct sigaction SIGUSR1_action = {0};
    SIGUSR1_action.sa_handler = handle_SIGUSR1;
    sigfillset(&SIGUSR1_action.sa_mask);
    SIGUSR1_action.sa_flags = 0;
    sigaction(SIGUSR1, &SIGUSR1_action, NULL);

    printf("\n$ smallsh\n");
    fflush(stdout);
    int exit_status = 0;
    globalCommand = prompt();
    while ((globalCommand == NULL) || (strncmp(globalCommand->command, "exit", 4) != 0))
    {
        if (globalCommand != NULL)
        {
            if (strncmp(globalCommand->command, "cd", 2)==0)
            {
                cd(globalCommand);
            }
            else if (strncmp(globalCommand->command, "status", 6)==0)
            {
                status(exit_status);
            }
            else if (*(globalCommand->in_redirect_bool) != 0 || *(globalCommand->out_redirect_bool) != 0 || (*(globalCommand->bg) != 0))
            {
                exit_status = io_redirection(globalCommand);
            }
            else
            {
                exit_status = otherCommands(globalCommand);
            }
        }
        if (globalCommand != NULL)
        {
            // free input after each time if a command was entered, not needed for blank or comments, create a free function
            freeGlobalCommand(globalCommand);
        }
        globalCommand = prompt();
    }
    // free input after exit
    freeGlobalCommand(globalCommand);
    printf("Freeing %d children\n", n_children);
    raise(SIGUSR1);
}