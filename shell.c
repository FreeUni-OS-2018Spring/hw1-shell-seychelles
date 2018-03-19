#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function
 * parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(char** command);
int cmd_help(char** command);
int cmd_pwd(char** command);
int cmd_cd(char** command);
int cmd_ulimit(char** command);
int cmd_kill(char** command);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(char** command);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", "print working directory"},
    {cmd_cd, "cd", "change directory"},
    {cmd_ulimit, "ulimit", "modify shell resource limits"},
    {cmd_kill, "kill", "send signal to a process"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused char** command) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused char** command) { exit(0); }

/* Prints working directory */
int cmd_pwd(unused char** command) {
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) != NULL)
    fprintf(stdout, "%s\n", cwd);
  else
    perror("couldn't get working directory");
  return 0;
}

/* Changes working directory */
int cmd_cd(char** command) {
  char* path = command[1];  // need error checking.
  if (chdir(path) != 0) {
    fprintf(stdout, "cd: %s: No such file or directory\n", path);
  }
  return 0;
}

size_t get_length(char** command) {
  size_t length = 0;
  for (int i = 1;; i++) {
    char* arg = command[i];
    if (arg == NULL) break;
    length++;
  }
  return length;
}

int is_number(char* str) {
  int length = strlen(str);
  for (int i = 0; i < length; i++)
    if (!isdigit(str[i])) return 0;
  return 1;
}

int cmd_kill(char** command) {
  int pid, signal;
  if (get_length(command) < 1) {
    fprintf(stdout, "arguments must be process or job IDs\n");
    return 1;
  }

  if (is_number(command[1])) {
    pid = atoi(command[1]);

    // if single arugment is given default signal to SIGTERM.
    signal = get_length(command) == 2 && is_number(command[2])
                 ? atoi(command[2])
                 : SIGTERM;
  }

  if (kill(pid, signal) < 0) {
    fprintf(stdout, "no such process\n");
    return 1;
  }

  return 0;
}

int cmd_ulimit(char** command) { return 1; }

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  return -1;
}

/* Pipe redirection */
int redirected_execution(struct command* full_command) {
  char* program;
  char** args;
  size_t commands_count = commands_get_length(full_command);
  int fds1[2];
  int fds2[2];
  int* read_pipe = fds1;  // Read from 0 write to 1.
  int* write_pipe = fds2;

  for (size_t i = 0; i < commands_count; i++) {
    args = commands_get_cmd(full_command, i);
    program = args[0];
    if (i < commands_count - 1) {
      pipe(write_pipe);
    }

    pid_t pid = fork();
    if (pid < 0) {
      return -2;
    } else if (pid == 0) { /* Child Process */
      if (i == 0) {        /* First process, only writes to pipe. */
        close(write_pipe[0]);
        dup2(write_pipe[1], 1);
        // Here can be file input redirect.
      } else if (i ==
                 commands_count - 1) { /* Last process, only reads from pipe. */
        dup2(read_pipe[0], 0);
        // Here can be file output redirect.
      } else { /* Middle process, reads from pipe and writes to next pipe. */
        close(write_pipe[0]);
        dup2(read_pipe[0], 0);
        dup2(write_pipe[1], 1);
      }
      execv(program, args);  // Checking for built in commands.
      exit(1);
    } else { /* Parent Process */
      if (i < commands_count - 1) {
        close(write_pipe[1]);
      }
      waitpid(pid, NULL, 0);
      if (i > 0) {
        close(read_pipe[0]);
      }
      int* tmp = read_pipe;
      read_pipe = write_pipe;
      write_pipe = tmp;
    }
  }
  return 0;
}

int execute_command(char** command) {
  int status = 1;
  int fundex = lookup(command[0]);
  /* Find which built-in function to run. */
  if (fundex >= 0) {
    status = cmd_table[fundex].fun(command);
  } else {
    char* program = command[0];
    if (access(program, 0) < 0) { /* Check if program exists */
      printf("%s: command not found\n", program);
      return 1;
    }
    pid_t pid = fork(); /* Create a child process */
    if (pid < 0) {
      return 1;            /* Fork Failed error */
    } else if (pid == 0) { /* Child Process */
      execv(program, command);
      exit(1);
    } else { /* Parent Process */
      wait(&status);
    }
  }
  return status;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell
     * until it becomes a foreground process. We use SIGTTIN to pause the shell.
     * When the shell gets moved to the foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char* argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive) fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into commands with it's arguments. */
    struct command* full_command = parse(line);
    char** command = commands_get_cmd(full_command, 0);

    if (strcmp(line, "\n")) {
      if (command != NULL) {
        if (commands_get_length(full_command) > 1) {  // Pipes Handling
          redirected_execution(full_command);
        } else {
          execute_command(command);
        }
      } else {
        fprintf(stdout, "Syntax error!\n");
      }
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    commands_destroy(full_command);
  }

  return 0;
}