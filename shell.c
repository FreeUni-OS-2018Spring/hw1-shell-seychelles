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
#include <fcntl.h>

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

/* Checks if program exists and if not searching in PATH */
char* find_program(char* program_path) {
  if (access(program_path, 0) >= 0) {
    return program_path;
  }

  	char program_sufix_path[strlen(program_path)+1];
    char* res="/";
    strcpy(program_sufix_path, res);
    strcpy(program_sufix_path+1, program_path); //  example ->  program_sufix_path="/program_path"

    char* env = getenv("PATH");

   	int start=0;
   	for (int i=0;i<strlen(env);i++){
   		if(env[i]==':'){
  			char current_env[i-start+1];
  		    strncpy(current_env, &env[start],i-start);
  		    current_env[i-start]='\0';

  		    // concatenate (current_env) and (program_sufix_path)
  		    char res[strlen(current_env)+strlen(program_sufix_path)];

  		    strcpy(res, &current_env);
  		    strcpy(res+strlen(current_env), &program_sufix_path);

    	  	if(access(&res,0)>=0){
    	  		 return strdup(&res);
    	  	}
   			  start=i+1;
   		}
   	}

   	// if input program_path is at the end of the PATH
  	char last_attempt[strlen(&env[start])+strlen(program_sufix_path)];
  	strcpy(last_attempt, &env[start]);
  	strcpy(last_attempt+strlen(&env[start]), &program_sufix_path);

	if(access(&last_attempt,0)>=0){
		return strdup(&last_attempt);
    }

  /* Here must be search in PATH */
  fprintf(stderr, "%s:command not found\n", program_path);
  return NULL;
}

int redirected_execution(struct command* full_command, int inp_fd, int out_fd) {
  char** args;
  size_t commands_count = commands_get_length(full_command);
  int status = 1;
  int fds1[2];
  int fds2[2];
  int* read_pipe = fds1; // Read from 0 write to 1.
  int* write_pipe = fds2;

  for (size_t i = 0; i < commands_count; i++) {
    args = commands_get_cmd(full_command, i);
    if (i < commands_count - 1) pipe(write_pipe);

    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "Creating child process failed\n");
      return 1;
    } else if (pid == 0) { /* Child Process */
      if (i == 0) { /* First process, only writes to pipe. */
        if (commands_count != 1) { /* pipeless case */
          close(write_pipe[0]);
          dup2(write_pipe[1], STDOUT_FILENO);
        }
        if (inp_fd != STDIN_FILENO) {
          dup2(inp_fd, STDIN_FILENO);
        }
      }
      if (i == commands_count - 1) { /* Last process, only reads from pipe. */
        if (commands_count != 1) { /* pipeless case */
          dup2(read_pipe[0], STDIN_FILENO);
        }
        if (out_fd != STDOUT_FILENO) {
          dup2(out_fd, STDOUT_FILENO);
        }
      } 
      if(i != 0 && i != commands_count - 1) { /* Middle process, reads from pipe and writes to next pipe. */
        close(write_pipe[0]);
        dup2(read_pipe[0], 0);
        dup2(write_pipe[1], 1);
      }

      int fundex = lookup(args[0]);
      if (fundex >= 0) {
        int status = cmd_table[fundex].fun(args);
        exit(status);
      } else {
        char* program_path = find_program(args[0]);
        if (program_path != NULL) execv(program_path, args);
        exit(1);
      }

    } else { /* Parent Process */
      if (i < commands_count - 1) close(write_pipe[1]);
      waitpid(pid, &status, 0);
      if (i > 0) close(read_pipe[0]);
      int* tmp = read_pipe;
      read_pipe = write_pipe;
      write_pipe = tmp;
    }
  }
  return status;
}

int execute_command(char** args) {
  int status = 1;
  int fundex = lookup(args[0]); /* Find which built-in function to run. */
  if (fundex >= 0) {
    status = cmd_table[fundex].fun(args);
  } else {
    char* program_path = find_program(args[0]);
    if (program_path == NULL) return status;
    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "Creating child process failed\n");
      return 1;
    } else if (pid == 0) { /* Child Process */
      execv(program_path, args);
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
    struct command* full_command = parse(line);
    
    int inp_fd = STDIN_FILENO;
    int out_fd = STDOUT_FILENO;
    int is_redirection = 0;

    if (strcmp(line, "\n")) {
      if (full_command != NULL) { // Valid input

        char* filename;
        if ((filename = commands_get_inp_file(full_command)) != NULL) {
          int fd = open(filename, O_RDONLY);
          if (fd != -1) {
            inp_fd = fd;
            is_redirection = 1;
          } else {
            fprintf(stderr, "%s: could not open file\n", filename);
            is_redirection = -1;
          }
        }
        if ((filename = commands_get_out_file(full_command)) != NULL) {
          mode_t f_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
          int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, f_mode);
          if (fd != -1) {
            out_fd = fd;
            is_redirection = 1;
          } else {
            fprintf(stderr, "%s: could not create file\n", filename);
            is_redirection = -1;
          }
        }

        if (commands_get_length(full_command) > 1 || is_redirection == 1) { // Pipes
          redirected_execution(full_command, inp_fd, out_fd);
        } else if (is_redirection == 0) {
          execute_command(commands_get_cmd(full_command, 0));
        }
      } else {
        fprintf(stderr, "Syntax error!\n");
      }
    }

    if (inp_fd != STDIN_FILENO) {
      close(inp_fd);
    }
    if (out_fd != STDOUT_FILENO) {
      close(out_fd);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    commands_destroy(full_command);
  }

  return 0;
}
