#pragma once

/* A struct that represents a list of commands splitted with special characters. (| ...) */
struct command {
  size_t cmds_length; /* How many commands are there? */
  char*** cmds;
  char* inp_file;
  char* out_file;
  int append_to_file;
};

/* Parse line entered in terminal. */
struct command* parse(const char* line);

/* Get me the Nth command (zero-indexed) */
char** command_get_cmd(struct command* cmds, size_t n);

/* Free the memory */
void command_destroy(struct command* cmds);
