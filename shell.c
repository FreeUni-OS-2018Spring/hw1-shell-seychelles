#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
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

/* Currently active process group in foreground */
pid_t active_pgid = -1;

/* Currently active process in foreground */
pid_t active_pid = -1;

/* Count of processes in running state */
pid_t background_process_count = 0;

/* Env Variables Map */
simple_map variables;

int cmd_exit(char** command);
int cmd_help(char** command);
int cmd_pwd(char** command);
int cmd_cd(char** command);
int cmd_ulimit(char** command);
int cmd_kill(char** command);
int cmd_type(char** command);
int cmd_echo(char** command);
int cmd_wait(char** command);
int cmd_export(char** command);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(char** command);
typedef void (*limits)(int, int, char*, bool, bool, bool);

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
    {cmd_type, "type", "display information about command type"},
    {cmd_echo, "echo", "prints input to standard output"},
    {cmd_wait, "wait", "waits all children to terminate"},
    {cmd_export, "export", "exports variable to environment"}};

/* Prints a helpful description for the given command */
int cmd_help(unused char** command) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(char** command) {
  int status = 0;
  if (command[1] != NULL) {
    status = atoi(command[1]);
  }
  simple_map_dispose(&variables);
  exit(status);
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
    perror("cd: %s: No such file or directory\n");
  }
  return 0;
}

/* Waits children to terminate */
int cmd_wait(unused char** command) {
  int status = 1;
  for (int i = 0; i < background_process_count; i++) {
    wait(&status);
  }
  background_process_count = 0;
  return status;
}

int cmd_echo(char** command) {
  if (command[1] == NULL) {
    fprintf(stdout, "\n");
    return 0;
  }
  size_t command_length = get_length(command);
  for (int i = 1; i < command_length; i++) {
    fprintf(stdout, "%s ", command[i]);
  }
  fprintf(stdout, "%s\n", command[command_length]);
  return 0;
}

int cmd_export(char** command) {
  size_t arg_length = get_length(command);
  if (arg_length == 1) {  // Only export
    char* value = simple_map_get(&variables, command[1]);
    if (value == NULL)
      fprintf(stderr, "export: %s: No such variable\n", command[1]);
    else
      setenv(command[1], value, 0);
  } else if (arg_length == 2) {  // Definition and export
    char* name = strdup(command[1]);
    char* value = strdup(command[2]);
    simple_map_put(&variables, name, value);
    setenv(name, value, 0);
  } else {
    // Error
  }
  return 0;
}

int is_number(char* str) {
  int i = 0;
  int length = strlen(str);
  if (str[0] == '-') {
    i++;
    length--;
  }
  for (; i < length; i++)
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
    if (signal < 0) signal = -signal;
  }

  if (kill(pid, signal) < 0) {
    perror("no such process\n");
    return 1;
  }

  return 0;
}

int get_pipe_size() {
  int file_descriptors[2];
  pipe(file_descriptors);
  int size = fcntl(file_descriptors[1], F_GETPIPE_SZ);
  close(file_descriptors[1]);
  close(file_descriptors[0]);

  return size / 8 / 512;
}

void limit_helper(char* flaga, char* flagb, limits function) {
  bool is_soft;
  if (flaga[1] == 'H')
    is_soft = false;
  else
    is_soft = true;

  char f = flaga[strlen(flaga) - 1];
  int value = flagb && is_number(flagb) ? atoi(flagb) : 0;
  // fprintf(stdout, "%d %c\n", strlen(flag), f);

  switch (f) {
    case 'a':
      function(RLIMIT_CORE, value,
               strdup("core file size          (blocks, -c)"), is_soft, true,
               false);
      function(RLIMIT_DATA, value,
               strdup("data seg size           (kbytes, -d)"), is_soft, true,
               true);
      function(RLIMIT_NICE, value,
               strdup("scheduling priority             (-e)"), is_soft, true,
               false);
      function(RLIMIT_FSIZE, value,
               strdup("file size               (blocks, -f)"), is_soft, true,
               false);
      function(RLIMIT_SIGPENDING, value,
               strdup("pending signals                 (-i)"), is_soft, true,
               false);
      function(RLIMIT_MEMLOCK, value,
               strdup("max locked memory       (kbytes, -l)"), is_soft, true,
               true);
      function(RLIMIT_RSS, value,
               strdup("max memory size         (kbytes, -m)"), is_soft, true,
               true);
      function(RLIMIT_NOFILE, value,
               strdup("open files                      (-n)"), is_soft, true,
               false);
      fprintf(stdout, "pipe size            (512 bytes, -p) %d\n",
              get_pipe_size());
      function(RLIMIT_MSGQUEUE, value,
               strdup("POSIX message queues     (bytes, -q)"), is_soft, true,
               false);
      function(RLIMIT_RTPRIO, value,
               strdup("real-time priority              (-r)"), is_soft, true,
               false);
      function(RLIMIT_STACK, value,
               strdup("stack size              (kbytes, -s)"), is_soft, true,
               true);
      function(RLIMIT_CPU, value,
               strdup("cpu time               (seconds, -t)"), is_soft, true,
               false);
      function(RLIMIT_NPROC, value,
               strdup("max user processes              (-u)"), is_soft, true,
               true);
      function(RLIMIT_AS, value, strdup("virtual memory          (kbytes, -v)"),
               is_soft, true, true);
      function(RLIMIT_LOCKS, value,
               strdup("file locks                      (-x)"), is_soft, true,
               false);
      break;
    case 'c':
      function(RLIMIT_CORE, value,
               strdup("core file size          (blocks, -c)"), is_soft, false,
               false);
      break;
    case 'd':
      function(RLIMIT_DATA, value,
               strdup("data seg size           (kbytes, -d)"), is_soft, false,
               true);
      break;
    case 'e':
      function(RLIMIT_NICE, value,
               strdup("scheduling priority             (-e)"), is_soft, false,
               false);
      break;
    case 'f':
      function(RLIMIT_FSIZE, value,
               strdup("file size               (blocks, -f)"), is_soft, false,
               false);
      break;
    case 'i':
      function(RLIMIT_SIGPENDING, value,
               strdup("pending signals                 (-i)"), is_soft, false,
               false);
      break;
    case 'l':
      function(RLIMIT_MEMLOCK, value,
               strdup("max locked memory       (kbytes, -l)"), is_soft, false,
               true);
      break;
    case 'm':
      function(RLIMIT_RSS, value,
               strdup("max memory size         (kbytes, -m)"), is_soft, false,
               true);
      break;
    case 'n':
      function(RLIMIT_NOFILE, value,
               strdup("open files                      (-n)"), is_soft, false,
               false);
      break;
    case 'p': {
      fprintf(stdout, "%d\n", get_pipe_size());
      break;
    }
    case 'q':
      function(RLIMIT_MSGQUEUE, value,
               strdup("POSIX message queues     (bytes, -q)"), is_soft, false,
               false);
      break;
    case 'r':
      function(RLIMIT_RTPRIO, value,
               strdup("real-time priority              (-r)"), is_soft, false,
               false);
      break;
    case 's':
      function(RLIMIT_STACK, value,
               strdup("stack size              (kbytes, -s)"), is_soft, false,
               true);
      break;
    case 't':
      function(RLIMIT_CPU, value,
               strdup("cpu time               (seconds, -t)"), is_soft, false,
               false);
      break;
    case 'u':
      function(RLIMIT_NPROC, value,
               strdup("max user processes              (-u)"), is_soft, false,
               false);
      break;
    case 'v':
      function(RLIMIT_AS, value, strdup("virtual memory          (kbytes, -v)"),
               is_soft, false, true);
      break;
    case 'x':
      function(RLIMIT_LOCKS, value,
               strdup("file locks                      (-x)"), is_soft, false,
               false);
      break;
  }
}

void set_limit(int resource, int value, char* info, bool is_soft, bool print,
               bool kilobytes) {
  struct rlimit limit;
  getrlimit(resource, &limit);

  if (is_soft)
    limit.rlim_cur = value;
  else
    limit.rlim_max = value;

  setrlimit(resource, &limit);

  free(info);
}

void get_limit(int resource, int value, char* info, bool is_soft, bool print,
               bool kilobytes) {
  struct rlimit limit;
  getrlimit(resource, &limit);

  unsigned long soft_limit = (unsigned long)limit.rlim_cur;
  unsigned long hard_limit = (unsigned long)limit.rlim_max;

  if (print) fprintf(stdout, "%s ", info);
  if ((is_soft && soft_limit == RLIM_INFINITY) ||
      (!is_soft && hard_limit == RLIM_INFINITY)) {
    fprintf(stdout, "unlimited\n");
    free(info);
    return;
  }

  if (kilobytes) {
    soft_limit /= 1024;
    hard_limit /= 1024;
  }

  if (is_soft)
    fprintf(stdout, "%lu\n", soft_limit);
  else
    fprintf(stdout, "%lu\n", hard_limit);

  free(info);
}

int cmd_ulimit(char** command) {
  if (get_length(command) < 2) {
    limit_helper(command[1], NULL, get_limit);
    return 0;
  } else {
    limit_helper(command[1], command[2], set_limit);
    return 0;
  }
  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  return -1;
}

/* Checks if program exists and if not searching in PATH */
// >show_all_results< parameter says if method should print/log the paths
// default parameter value of is_builtin is -1
char* find_program(char* program_path, int show_all_results, int is_builtin) {
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

  char* final_res = NULL;
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
        if (show_all_results != 0) {
          fprintf(stdout, "%s is %s\n", program_path, res);
        }

        if (final_res == NULL) {
          final_res = strdup((const char*)&res);
        }
      }
      start = i + 1;
    }
  }
  // if input program_path is at the end of the PATH
  char last_attempt[strlen(&env[start]) + strlen(program_sufix_path)];
  strcpy(last_attempt, &env[start]);
  strcpy(last_attempt + strlen(&env[start]), (const char*)&program_sufix_path);

  if (access((const char*)&last_attempt, 0) >= 0) {
    if (show_all_results != 0) {
      fprintf(stdout, "%s is %s\n", program_path, last_attempt);
    }
    if (final_res == NULL) {
      final_res = strdup((const char*)&last_attempt);
    }
  }
  if (final_res != NULL) {
    return final_res;
  }

  /* Here must be search in PATH */
  if (is_builtin == -1) {
    fprintf(stderr, "%s: command not found\n", program_path);
  }
  return NULL;
}

int cmd_type(char** command) {
  char* current_command = command[1];
  int have_command = lookup(current_command);
  if (have_command != -1) {
    fprintf(stdout, "%s is a shell builtin\n", current_command);
  }
  find_program(current_command, 1, have_command);
  return 0;
}

int redirected_execution(struct command* full_command, int inp_fd, int out_fd) {
  int status = 1;
  int fds1[2];
  int fds2[2];
  int* read_pipe = fds1;  // Read from 0 write to 1.
  int* write_pipe = fds2;
  pid_t pgid = -1;
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
        char* program_path = find_program(args[0], 0, -1);
        if (program_path == NULL) exit(1);
        execv(program_path, args);
        exit(1);
      }

    } else { /* Parent Process */
      if (pgid == -1) pgid = pid;
      setpgid(pid, pgid);
      if (i < full_command->cmds_length - 1) close(write_pipe[1]);
      if (i > 0) close(read_pipe[0]);
      int* tmp = read_pipe;
      read_pipe = write_pipe;
      write_pipe = tmp;
    }
  }
  if (full_command->background == 0) {
    active_pgid = pgid;
    for (size_t i = 0; i < full_command->cmds_length;
         i++) /* Wait for all childs in pipe */
      waitpid(-pgid, &status, WSTOPPED);
    active_pgid = -1;
  } else {
    background_process_count += full_command->cmds_length;
  }
  return status;
}

int execute_command(char** args, int background, int env_var_definition) {
  int status = 0;
  int fundex = lookup(args[0]); /* Find which built-in function to run. */
  if (fundex >= 0) {
    status = cmd_table[fundex].fun(args);
  } else if (env_var_definition == 1) { /* Definition without export */
    char* name = strdup(args[0]);
    char* value = strdup(args[1]);
    simple_map_put(&variables, name, value);
  } else {
    char* program_path = find_program(args[0], 0, -1);
    if (program_path == NULL) return status;
    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "Creating child process failed\n");
      return 1;
    } else if (pid == 0) { /* Child Process */
      execv(program_path, args);
      exit(1);
    } else { /* Parent Process */
      setpgid(pid, pid);
      if (background == 0) {
        active_pid = pid;
        tcsetpgrp(shell_terminal, pid);
        waitpid(pid, &status, WSTOPPED);
        tcsetpgrp(shell_terminal, shell_pgid);
        active_pid = -1;
      } else {
        background_process_count++;
      }
    }
  }
  return status;
}

/* There's no handling for processes that were stopped */
void signal_handler(int signum) {
  if (signum == SIGINT || signum == SIGTSTP) {
    if (active_pid != -1) {
      kill(active_pid, signum);
    } else if (active_pgid != -1) {
      killpg(active_pgid, signum);
    }
  } else if (signum == SIGCHLD) {
    waitpid(-1, NULL, WSTOPPED);
  }
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

    signal(SIGTTOU, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTSTP, signal_handler);
    signal(SIGCHLD, signal_handler);
  }
}

char** str_split(char* str, const char delimiter) {
  char** result = 0;
  size_t count = 0;
  char* tmp = str;
  char* last_comma = 0;
  char delim[2];
  delim[0] = delimiter;
  delim[1] = 0;

  /* Count how many elements will be extracted. */
  while (*tmp) {
    if (delimiter == *tmp) {
      count++;
      last_comma = tmp;
    }
    tmp++;
  }

  /* Add space for trailing token. */
  count += last_comma < (str + strlen(str) - 1);

  /* Add space for terminating null string so caller
     knows where the list of returned strings ends. */
  count++;

  result = malloc(sizeof(char*) * count);

  if (result) {
    size_t index = 0;
    char* token = strtok(str, delim);

    while (token) {
      *(result + index++) = strdup(token);
      token = strtok(0, delim);
    }
    *(result + index) = 0;
  }

  return result;
}

void c_command(int argc, char* argv[]) {
  if (argc > 2 && (strcmp(argv[1], "-c") == 0)) {
    char** splitted = str_split(argv[2], ';');
    int length = get_length(splitted) + 1;
    for (int i = 0; i < length; i++) {
      printf("%s ", splitted[i]);
    }
  }
}

int main(int argc, char* argv[]) {
  init_shell();

  simple_map_new(&variables);
  simple_map_put(&variables, strdup("?"), strdup("0"));

  static char line[4096];
  int line_num = 0;

  c_command(argc, argv);
  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive) fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into commands with it's arguments. */
    struct command* full_command = parse(line, &variables);

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
          char** args = command_get_cmd(full_command, 0);
          execute_command(args, full_command->background,
                          full_command->env_var_definition);
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
