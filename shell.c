#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <ulimit.h>
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
int cmd_type(char** command);

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
    {cmd_type, "type", "display information about command type"}};

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
    perror("arguments must be process or job IDs");
    return 1;
  }

  if (is_number(command[1])) {
    pid = get_length(command) == 1 && is_number(command[1]) ? atoi(command[1])
                                                            : atoi(command[2]);

    // if single arugment is given default signal to SIGTERM.
    signal = get_length(command) == 2 && is_number(command[1])
                 ? atoi(command[1])
                 : SIGTERM;
  }

  if (kill(pid, signal) < 0) {
    perror("no such process\n");
    return 1;
  }

  return 0;
}

int get_limit_resources(char* flag, bool* is_soft) {
  if (flag[1] == 'H')
    *is_soft = false;
  else
    *is_soft = true;

  size_t length = strlen(flag);
  char f = flag[length - 1];
  // fprintf(stdout, "%d %c\n", strlen(flag), f);

  switch (f) {
    case 'a':
      fprintf(stdout,
              "karoche dzmao ar mushaobs ra esa "
              "jerisjerobita)))))))))))))))))))))))))))))))) kiorneli");
      return RLIMIT_DATA;
    case 'c':
      return RLIMIT_CORE;
    case 'd':
      return RLIMIT_DATA;
    case 'e':
      return RLIMIT_NICE;
    case 'f':
      return RLIMIT_FSIZE;
    case 'i':
      return RLIMIT_SIGPENDING;
    case 'l':
      return RLIMIT_MEMLOCK;
    case 'm':
      return RLIMIT_RSS;
    case 'n':
      return RLIMIT_NOFILE;
    case 'p':
      return RLIMIT_NOFILE;
    case 'q':
      return RLIMIT_MSGQUEUE;
    case 'r':
      return RLIMIT_RTPRIO;
    case 's':
      return RLIMIT_STACK;
    case 't':
      return RLIMIT_CPU;
    case 'u':
      return RLIMIT_NPROC;
    case 'v':
      return RLIMIT_AS;
    case 'x':
      return RLIMIT_LOCKS;
  }

  return 0;
}

void set_limit(char* flaga, char* flagb) {
  struct rlimit limit;
  bool is_soft = true;
  int resource = get_limit_resources(flaga, &is_soft);
  getrlimit(resource, &limit);

  if (!is_number(flagb)) perror("invalid number");

  if (is_soft)
    limit.rlim_cur = atoi(flagb);
  else
    limit.rlim_max = atoi(flagb);

  setrlimit(resource, &limit);

  return;
}

void get_limit(char* flag) {
  struct rlimit limit;
  bool is_soft = true;
  getrlimit(get_limit_resources(flag, &is_soft), &limit);

  if (is_soft)
    fprintf(stdout, "%d\n", (int)limit.rlim_cur);
  else
    fprintf(stdout, "%d\n", (int)limit.rlim_max);
}

int cmd_ulimit(char** command) {
  if (get_length(command) < 2) {
    get_limit(command[1]);
  } else {
    set_limit(command[1], command[2]);
  }
  return 1;
}


/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  return -1;
}

int cmd_type(char** command);

/* Checks if program exists and if not searching in PATH */
// >show_all_results< parameter says if method should print/log the paths
char* find_program(char* program_path,int show_all_results) {
  if (access(program_path, 0) >= 0) {
    return program_path;
  }

  char program_sufix_path[strlen(program_path) + 1];
  char* res = "/";
  strcpy(program_sufix_path, res);
  strcpy(program_sufix_path + 1,
         program_path);  //  example ->  program_sufix_path="/program_path"

  char* env = getenv("PATH");

  int start = 0;

  char* final_res=NULL;
  for (int i = 0; i < strlen(env); i++) {
    if (env[i] == ':') {
      char current_env[i - start + 1];
      strncpy(current_env, &env[start], i - start);
      current_env[i - start] = '\0';
      // concatenate (current_env) and (program_sufix_path)
      char res[strlen(current_env) + strlen(program_sufix_path)];

      strcpy(res, (const char*)&current_env);
      strcpy(res + strlen(current_env), (const char*)&program_sufix_path);
      if (access((const char*)&res, 0) >= 0) {
      	if(show_all_results!=0){
      		fprintf(stderr, "%s is %s\n",program_path, res);
      	}

      	if(final_res==NULL){
      		final_res=strdup((const char*)&res);
      	}
        //return strdup((const char*)&res);
      }
      start = i + 1;
    }
  }
  // if input program_path is at the end of the PATH
  char last_attempt[strlen(&env[start]) + strlen(program_sufix_path)];
  strcpy(last_attempt, &env[start]);
  strcpy(last_attempt + strlen(&env[start]), (const char*)&program_sufix_path);

  if (access((const char*)&last_attempt, 0) >= 0) {
  	if(show_all_results!=0){
  		fprintf(stderr, "%s is %s\n",program_path, last_attempt);
  	}
  	if(final_res==NULL){
  		final_res=strdup((const char*)&last_attempt);
  	}
    //return strdup((const char*)&last_attempt);
  }
  if(final_res!=NULL){
  	return final_res;
  }

  /* Here must be search in PATH */
  fprintf(stderr, "%s:command not found\n", program_path);
  return NULL;
}

int cmd_type(char** command) {
  	char* current_command=command[1];
  	int have_command =  lookup(current_command);
  	if(have_command!=-1){
  		fprintf(stderr, "%s is a shell builtin\n", current_command);
  	}
	find_program(current_command,1);
  return 0;
}


int redirected_execution(struct command* full_command, int inp_fd, int out_fd) {
  int status = 1;
  int fds1[2];
  int fds2[2];
  int* read_pipe = fds1;  // Read from 0 write to 1.
  int* write_pipe = fds2;

  for (size_t i = 0; i < full_command->cmds_length; i++) {
    char** args = command_get_cmd(full_command, i);

    if (i <
        full_command->cmds_length - 1) /* Don't create pipe for last process */
      pipe(write_pipe);

    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "Creating child process failed\n");
      return 1;
    } else if (pid == 0) { /* Child Process */
      if (i == 0) {        /* First process, only writes to pipe. */
        if (full_command->cmds_length != 1) { /* Check for pipeless case */
          close(write_pipe[0]);
          dup2(write_pipe[1], STDOUT_FILENO);
        }
        if (inp_fd != STDIN_FILENO) {
          dup2(inp_fd, STDIN_FILENO);
        }
      }
      if (i == full_command->cmds_length -
                   1) { /* Last process, only reads from pipe. */
        if (full_command->cmds_length != 1) { /* Check for pipeless case */
          dup2(read_pipe[0], STDIN_FILENO);
        }
        if (out_fd != STDOUT_FILENO) {
          dup2(out_fd, STDOUT_FILENO);
        }
      }
      if (i != 0 &&
          i != full_command->cmds_length -
                   1) { /* Middle process, reads from pipe and writes to next
                           pipe. */
        close(write_pipe[0]);
        dup2(read_pipe[0], 0);
        dup2(write_pipe[1], 1);
      }

      int fundex = lookup(args[0]);
      if (fundex >= 0) {
        int status = cmd_table[fundex].fun(args);
        exit(status);
      } else {
        char* program_path = find_program(args[0],0);
        if (program_path == NULL) exit(1);
        execv(program_path, args);
        exit(1);
      }

    } else { /* Parent Process */
      if (i < full_command->cmds_length - 1) close(write_pipe[1]);
      if (i > 0) close(read_pipe[0]);
      int* tmp = read_pipe;
      read_pipe = write_pipe;
      write_pipe = tmp;
    }
  }
  for (size_t i = 0; i < full_command->cmds_length;
       i++) /* Wait for all childs */
    wait(&status);
  return status;
}

int execute_command(char** args) {
  int status = 1;
  int fundex = lookup(args[0]); /* Find which built-in function to run. */
  if (fundex >= 0) {
    status = cmd_table[fundex].fun(args);
  } else {
    char* program_path = find_program(args[0],0);
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
    /* If the shell is not currently in the foreground, we must pause the
     * shell until it becomes a foreground process. We use SIGTTIN to pause
     * the shell. When the shell gets moved to the foreground, we'll receive a
     * SIGCONT. */
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

    int inp_fd = STDIN_FILENO;
    int out_fd = STDOUT_FILENO;
    int is_redirection = 0;

    if (strcmp(line, "\n")) {
      if (full_command != NULL) {  // Valid input

        if (full_command->inp_file != NULL) {  // Prepare file if neccessary
          int fd = open(full_command->inp_file, O_RDONLY);
          if (fd != -1) {
            inp_fd = fd;
            is_redirection = 1;
          } else {
            fprintf(stderr, "%s: could not open file\n",
                    full_command->inp_file);
            is_redirection = -1;
          }
        }
        if (full_command->out_file != NULL) {  // Prepare file if neccessary
          mode_t f_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
          int f_flags;
          if (full_command->append_to_file == 1)
            f_flags = O_WRONLY | O_CREAT | O_APPEND;
          else
            f_flags = O_WRONLY | O_CREAT | O_TRUNC;
          int fd = open(full_command->out_file, f_flags, f_mode);
          if (fd != -1) {
            out_fd = fd;
            is_redirection = 1;
          } else {
            fprintf(stderr, "%s: could not open file\n",
                    full_command->out_file);
            is_redirection = -1;
          }
        }

        if (full_command->cmds_length > 1 ||
            is_redirection == 1) {  // Pipes and redirection.
          redirected_execution(full_command, inp_fd, out_fd);
          if (inp_fd != STDIN_FILENO) close(inp_fd);
          if (out_fd != STDOUT_FILENO) close(out_fd);
        } else if (is_redirection == 0) {
          execute_command(command_get_cmd(full_command, 0));
        }
      } else {
        fprintf(stderr, "Syntax error!\n");
      }
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    command_destroy(full_command);
  }

  return 0;
}
