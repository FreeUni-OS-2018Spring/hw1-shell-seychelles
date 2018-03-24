#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "tokenizer.h"

static void *vector_push(void* pointer, size_t* size, void* elem) {
  void*** ptr = (void***)pointer;
  *ptr = realloc(*ptr, sizeof(void*) * (*size + 1));
  (*ptr)[*size] = elem;
  *size += 1;
  return elem;
}

static void *copy_word(char *source, size_t n) {
  source[n] = '\0';
  char *word = (char *) malloc(n + 1);
  strncpy(word, source, n + 1);
  return word;
}

struct command* parse(const char *line) {
  if (line == NULL) {
    return NULL;
  }

  static char token[4096];
  size_t n = 0, n_max = 4096;
  size_t line_length = strlen(line);

  struct command* cmds = (struct command *) malloc(sizeof(struct command));
  cmds->cmds_length = 0;
  cmds->cmds = NULL;
  cmds->inp_file = NULL;
  cmds->out_file = NULL;
  cmds->append_to_file = 0;
  cmds->background = 0;

  const int MODE_NORMAL = 0,
        MODE_SQUOTE = 1,
        MODE_DQUOTE = 2;
  int mode = MODE_NORMAL;

  char** cmd = NULL;
  size_t cmd_len = 0;
  int input_filename = 0;
  int output_filename = 0;

  for (unsigned int i = 0; i < line_length; i++) {
    char c = line[i];
    if (mode == MODE_NORMAL) {
      if (c == '\'') {
        mode = MODE_SQUOTE;
      } else if (c == '"') {
        mode = MODE_DQUOTE;
      } else if (c == '\\') {
        if (i + 1 < line_length) {
          token[n++] = line[++i];
        }
      } else if (isspace(c)) {
        if (n > 0) {
          if (input_filename == 1) {
            input_filename = 0;
            cmds->inp_file = (char*)copy_word(token, n);
          } else if (output_filename == 1) {
            output_filename = 0;
            cmds->out_file = (char*)copy_word(token, n);
          } else {
            void *word = copy_word(token, n);
            vector_push(&cmd, &cmd_len, word);
          }
          n = 0;
        }
      } else if (c == '|') { // Pipe support.
        // There must be some command before pipe operator.
        vector_push(&cmd, &cmd_len, NULL); // Append NULL terminator.
        vector_push(&cmds->cmds, &cmds->cmds_length, cmd);
        cmd = NULL;
        cmd_len = 0;
        n = 0;
      } else if (c == '<') {
        // There must be some command before redirect operator.
        input_filename = 1;
      } else if (c == '>' && line[i + 1] == '>') {
        // There must be some command before redirect operator.
        output_filename = 1;
        cmds->append_to_file = 1;
        i++;
      } else if (c == '>') {
        // There must be some command before redirect operator.
        output_filename = 1;
      } else if (c == '&') {
        // This operator must be at the and of the line and separated with spaces.
        cmds->background = 1;
      } else {
        token[n++] = c;
      }
    } else if (mode == MODE_SQUOTE) {
      if (c == '\'') {
        mode = MODE_NORMAL;
      } else if (c == '\\') {
        if (i + 1 < line_length) {
          token[n++] = line[++i];
        }
      } else {
        token[n++] = c;
      }
    } else if (mode == MODE_DQUOTE) {
      if (c == '"') {
        mode = MODE_NORMAL;
      } else if (c == '\\') {
        if (i + 1 < line_length) {
          token[n++] = line[++i];
        }
      } else {
        token[n++] = c;
      }
    }
    if (n + 1 >= n_max) abort();
  }

  if (n > 0) {
    void *word = copy_word(token, n);
    vector_push(&cmd, &cmd_len, word);
  }
  if (cmd_len > 0) {
    vector_push(&cmd, &cmd_len, NULL); // Append NULL terminator.
    vector_push(&cmds->cmds, &cmds->cmds_length, cmd);
  }
  return cmds;
}

char** command_get_cmd(struct command* cmds, size_t n) {
  if (cmds == NULL || n >= cmds->cmds_length) {
    return NULL;
  } else {
    return cmds->cmds[n];
  }
}

void command_destroy(struct command* cmds) {
  if (cmds == NULL) {
    return;
  }
  for (int i = 0; i < cmds->cmds_length; i++) {
    char** cmd = cmds->cmds[i];
    int index = 0;
    while(1) {
      if (cmd[index] == NULL) break;
      free(cmd[index++]);
    }
    free(cmd);
  }
  if (cmds->cmds) {
    free(cmds->cmds);
  }
  if (cmds->inp_file) {
    free(cmds->inp_file);
  }
  if (cmds->out_file) {
    free(cmds->out_file);
  }
  free(cmds);
}
