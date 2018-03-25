#pragma once
#include "simple_map.h"

/* A struct that represents a list of commands splitted with special characters. (| ...) */
struct command {
  size_t cmds_length; /* How many commands are there? */
  char*** cmds;
  char* inp_file;
  char* out_file;
  int append_to_file;
  int background;
  int env_var_definition;
};

/* Parse line entered in terminal. */
struct command* parse(const char* line, simple_map* variables);

/* Get me the Nth command (zero-indexed) */
char** command_get_cmd(struct command* cmds, size_t n);

/* Free the memory */
void command_destroy(struct command* cmds);
