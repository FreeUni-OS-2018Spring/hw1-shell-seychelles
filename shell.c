#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
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

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(char** command);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "print working directory"},
  {cmd_cd, "cd", "change directory"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused char** command) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused char** command) {
  exit(0);
}

/* Prints working directory */
int cmd_pwd(unused char** command) {
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) != NULL)
    fprintf(stdout, "%s\n", cwd);
  else
    perror("couldn't get working directory");
  return 1;
}

/* Changes working directory */
int cmd_cd(char** command) {
  char* path = command[1]; // need error checking.
  if(chdir(path) != 0) {
    fprintf(stdout, "cd: %s: No such file or directory\n", path);
  }
  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

int redirected_execution(struct command* command) {
  char** args;
  size_t commands_count = commands_get_length(command);

  /* Prepare Pipes, they are not deallocated. */
  int* pipe_fds[commands_count - 1];
  for (int i = 0; i < commands_count - 1; i++) {
    int* f_des = malloc(2 * sizeof(int)); // Read from 0 write to 1.
    pipe(f_des);
    pipe_fds[i] = f_des;
  }
  
  for (size_t i = 0; i < commands_count; i++) {
    args = commands_get_cmd(command, i);

    pid_t pid = fork();
    if (pid < 0) {
      return -2;
    } else if (pid == 0) { /* Child Process */
      /* Here all unused file descriptors must be closed, but are not in this code. */
      if (i == 0) { /* First process, only writes to pipe. */
        dup2(pipe_fds[0][1], 1);
        // Here can be file input redirect.
      } else if (i == commands_count - 1) { /* Last process, only reads from pipe. */
        dup2(pipe_fds[i - 1][0], 0);
        // Here can be file output redirect.
      } else { /* Middle process, reads from pipe and writes to next pipe. */
        dup2(pipe_fds[i - 1][0], 0);
        dup2(pipe_fds[i][1], 1);
      }
      execv(args[0], args);
      exit(1);
    } else { /* Parent Process */
      if (i < commands_count - 1) {
        close(pipe_fds[i][1]); // Close current write.
      }
      waitpid(pid, NULL, 0);
      if (i > 0) {
        close(pipe_fds[i - 1][0]); // Close previous read.
      }
    }
  }
  return 0;
}

int execute_command(char** command) {
  int status = 1;
  int fundex = lookup(command[0]);
  if (fundex >= 0) {
    status = cmd_table[fundex].fun(command);
  } else {
    char* program = command[0];
    if (access(program, 0) < 0) { /* Check if program exists */
      return -1; /* Does Not Exists */
    }
    pid_t pid = fork(); /* Create a child process */
    if (pid < 0) {
      return -2; /* Fork Failed error */
    } else if (pid == 0) { /* Child Process */
      execv(program,command);
      exit(1); // if exec fails process dies.
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
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
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

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into commands with it's arguments. */
    struct command* command = parse(line);
    char** first_subcmd = commands_get_cmd(command, 0);
    
    if (strcmp(line, "\n")) {
      if (first_subcmd != NULL) {
        /* Find which built-in function to run. */
        int status;
        if (commands_get_length(command) > 1) { // Pipes Handling
          status = redirected_execution(command);
        } else {
          status = execute_command(first_subcmd);
        }
        if (status != 0) {
          fprintf(stdout, "This shell doesn't know how to run programs.\n");
        }
      } else {
        fprintf(stdout, "Syntax error!\n");
      }
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    commands_destroy(command);
  }

  return 0;
}
