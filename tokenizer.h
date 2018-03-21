#pragma once

/* A struct that represents a list of commands splitted with special characters. (|, <, > ...) */
struct command;

/* Parse line entered in terminal. */
struct command* parse(const char* line);

/* How many commands are there? */
size_t commands_get_length(struct command* cmds);

/* Get me the Nth command (zero-indexed) */
char** commands_get_cmd(struct command* cmds, size_t n);

char* commands_get_inp_file(struct command* cmds);

char* commands_get_out_file(struct command* cmds);

/* Free the memory */
void commands_destroy(struct command* cmds);
