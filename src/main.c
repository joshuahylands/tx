#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <linux/limits.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define ERROR_MSG_LENGTH 128
#define COMMAND_LINE_LENGTH 128

#define MODE_DEFAULT 0
#define MODE_CMD 1

#define KEY_UP -2
#define KEY_DOWN -3
#define KEY_RIGHT -4
#define KEY_LEFT -5
#define KEY_INSERT -6
#define KEY_ENTER '\n'
#define KEY_BACKSPACE '\b'
#define KEY_DELETE 0x7f

#define SETDIR_SELECTED -1
#define SETDIR_PARENT -2

#define ESC(code) "\x1b[" code
#define ENABLE_ALT_BUFFER ESC("?1049h")
#define DISABLE_ALT_BUFFER ESC("?1049l")
#define SET_CURSOR_INVISIBLE ESC("?25l")
#define SET_CURSOR_VISIBLE ESC("?25h")
#define SET_CURSOR_HOME ESC("H")
#define DELETE_ALL ESC("2J")
#define DELETE_LINE ESC("2K")

#define S_RESET ESC("0m")
#define ST_BOLD ESC("1m")
#define ST_UNDERLINE ESC("4m")
#define SF_ERROR ESC("31m")

typedef struct Terminal {
  struct termios old_settings;
  unsigned short lines;
  unsigned short columns;
} Terminal;
static Terminal g_terminal;

typedef struct Directory {
  char path[PATH_MAX];
  struct dirent** entries;
  struct stat* entries_stat;
  int entries_length;
  int selected_entry;
} Directory;
static Directory g_directory = {0};

typedef struct State {
  char error_msg[ERROR_MSG_LENGTH];
  int mode;
  char cmd_line[COMMAND_LINE_LENGTH];
  int cmd_line_length;
  int cmd_line_position;
} State;
static State g_state = {0};

void get_terminal_size(int _) {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0) {
    strcpy(g_state.error_msg, "Can't get terminal size");
    return;
  }

  g_terminal.lines = w.ws_row;
  g_terminal.columns = w.ws_col;
}

void set_cursor_line(int line) {
  int l = line;
  if (line < 0) {
    l = g_terminal.lines + line + 1;
  }

  printf("\x1b[%d;0H", l);
}

void set_cursor_column(int cols) {
  printf("\x1b[%dG", cols);
}

void reset_terminal(void) {
  tcsetattr(STDIN_FILENO, TCSANOW, &g_terminal.old_settings);
  printf(DISABLE_ALT_BUFFER SET_CURSOR_VISIBLE);
  fflush(stdout);
}

void init_terminal(void) {
  // get the current terminal configuration
  // and change some settings
  tcgetattr(STDIN_FILENO, &g_terminal.old_settings);

  struct termios new_settings = g_terminal.old_settings;
  new_settings.c_lflag &= ~(ICANON | ECHO);

  // setup terminal:
  printf(ENABLE_ALT_BUFFER DELETE_ALL SET_CURSOR_INVISIBLE SET_CURSOR_HOME);
  fflush(stdout);
  tcsetattr(STDIN_FILENO, TCSANOW, &new_settings);

  // get size and setup signal to listen for changes
  get_terminal_size(0);
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = get_terminal_size;
  if (sigaction(SIGWINCH, &sa, NULL) != 0) {
    strcpy(g_state.error_msg, "Can't setup signal action: SIGWINCH");
  }

  atexit(reset_terminal);
}

void cleanup_directory(void) {
  // free existing entries
  if (g_directory.entries != NULL) {
    for (int i = 0; i < g_directory.entries_length; i++) {
      free(g_directory.entries[i]);
    }
  }

  free(g_directory.entries_stat);

  g_directory = (Directory) {0};
}

void get_directory(void) {
  cleanup_directory();

  // get cwd
  if(getcwd(g_directory.path, sizeof(g_directory.path)) == NULL) {
    strcpy(g_state.error_msg, "Can't get current working directory");
    return;
  }

  // get entries
  g_directory.entries_length = scandir(
    g_directory.path,
    &g_directory.entries,
    NULL,
    alphasort
  );
  if (g_directory.entries_length == -1) {
    strcpy(g_state.error_msg, "Can't get directory entries");
    return;
  }

  // get status of each file
  g_directory.entries_stat = malloc(sizeof(struct stat) * g_directory.entries_length);
  if (g_directory.entries_stat == NULL) {
    strcpy(g_state.error_msg, "Failed to allocate memory for directory entry status");
    return;
  }

  for (int i = 0; i < g_directory.entries_length; i++) {
    if (stat(g_directory.entries[i]->d_name, &(g_directory.entries_stat[i])) == -1) {
      strcpy(g_state.error_msg, "Couldn't get file status");
    }
  }
}

void set_directory(int i) {
  // get the relative directory name
  char nextDir[FILENAME_MAX];
  if (i == SETDIR_SELECTED) {
    if (!S_ISDIR(g_directory.entries_stat[g_directory.selected_entry].st_mode)) {
      return;
    }
    
    strcpy(
      nextDir,
      g_directory.entries[g_directory.selected_entry]->d_name
    );
  } else if (i == SETDIR_PARENT) {
    strcpy(nextDir, "..");
  } else {
    return;
  }

  // make full path
  char newDir[PATH_MAX];
  snprintf(
    newDir,
    sizeof(newDir),
    "%s/%s",
    g_directory.path,
    nextDir
  );

  // change directory
  if (chdir(newDir) == -1) {
    strcpy(g_state.error_msg, "Couldn't change directory");
    return;
  }

  get_directory();
}

void display_directory(void) {
  // format and print path
  char pathTemp[PATH_MAX];
  strcpy(pathTemp, g_directory.path);
  char* part = strtok(pathTemp, "/");
  do {
    if (strcmp(part, "home") == 0) {
      printf(DELETE_LINE "\r ~ >");
    } else {
      printf(" %s >", part);
    }
  } while ((part = strtok(NULL, "/")) != NULL);
  printf("\b \n");

  // print all entries
  for (int i = 0; i < g_directory.entries_length; i++) {
    char fileType[4] = "   ";
    if (S_ISDIR(g_directory.entries_stat[i].st_mode)) {
      strcpy(fileType, "DIR");
    }

    if (i == g_directory.selected_entry) {
      printf(
        ST_BOLD "%s "
        ST_UNDERLINE "%s >"
        S_RESET "\n",

        fileType,
        g_directory.entries[i]->d_name
      );
    } else {
      printf(
        ST_BOLD "%s "
        S_RESET "%s\n",

        fileType,
        g_directory.entries[i]->d_name
      );
    }
  }
}

void display_state(void) {
  if (g_state.error_msg[0] != '\0') {
    set_cursor_line(-2);
    printf(ST_BOLD SF_ERROR "ERROR: %s\n" S_RESET, g_state.error_msg);
  }

  set_cursor_line(-1);

  switch (g_state.mode) {
  case MODE_DEFAULT:
    printf(ST_BOLD " DEFAULT ");
    break;
  case MODE_CMD:
    printf(ST_BOLD "   CMD   ");
    break;
  }

  printf("> " S_RESET "%s" ST_BOLD, g_state.cmd_line);

  if (g_state.mode == MODE_CMD) {
    // 12 is the number of columns used before the command
    set_cursor_column(g_state.cmd_line_position + 12);
    printf(SET_CURSOR_VISIBLE);
  }
}

void run_cmd_line(void) {
  system(g_state.cmd_line);
  memset(g_state.cmd_line, 0, COMMAND_LINE_LENGTH);
  g_state.cmd_line_position = 0;
  get_directory();
}

int get_input(void) {
  int input = getchar();

  if (input == '\x1b') {
    input = getchar();
    if (input == '[') {
      input = getchar();

      if (input == '2') {
        input = getchar();

        if (input == '~') {
          return KEY_INSERT;
        } else {
          return input;
        }
      }

      switch (input) {
      case 'A':
        return KEY_UP;
      case 'B':
        return KEY_DOWN;
      case 'C':
        return KEY_RIGHT;
      case 'D':
        return KEY_LEFT;
      }
    } else {
      input = getchar();
    }
  }

  return input;
}

void process_cmd_line_input(int input) {
  int cmdLineLength = strlen(g_state.cmd_line);

  if (input == KEY_ENTER) {
    if (cmdLineLength > 0) {
      run_cmd_line();
    }
  } else if (input == KEY_LEFT) {
    if (--g_state.cmd_line_position < 0) {
      g_state.cmd_line_position = 0;
    }
  } else if (input == KEY_RIGHT) {
    if (++g_state.cmd_line_position > cmdLineLength) {
      g_state.cmd_line_position = cmdLineLength;
    }
  } else if (input == KEY_BACKSPACE || input == KEY_DELETE) {
    if (cmdLineLength > 0) {
      strcpy(
        &g_state.cmd_line[g_state.cmd_line_position - 1],
        &g_state.cmd_line[g_state.cmd_line_position--]
      );
    }
  } else if (cmdLineLength < COMMAND_LINE_LENGTH - 1) {
    strcpy(&g_state.cmd_line[g_state.cmd_line_position + 1], &g_state.cmd_line[g_state.cmd_line_position]);
    g_state.cmd_line[g_state.cmd_line_position++] = (char) input;
  } 
}

int main(void) {
  init_terminal();
  get_directory();
  atexit(cleanup_directory);

  bool running = true;
  while (running) {
    printf(DELETE_ALL SET_CURSOR_HOME SET_CURSOR_INVISIBLE);
    display_directory();
    display_state();
    fflush(stdout);

    // get input then reset the error message
    int input = get_input();
    g_state.error_msg[0] = '\0';

    if (input == KEY_INSERT) {
      g_state.mode = !g_state.mode;
    } else if (g_state.mode == MODE_CMD) {
      process_cmd_line_input(input);
    } else if (input == 'q' || input == 'Q') {
      running = false;
    } else if (input == KEY_INSERT) {
      g_state.mode = !g_state.mode;
    } else if (
      input == KEY_UP &&
      --g_directory.selected_entry < 0
    ) {
      g_directory.selected_entry = g_directory.entries_length - 1;
    } else if (
      input == KEY_DOWN &&
      ++g_directory.selected_entry == g_directory.entries_length
    ) {
      g_directory.selected_entry = 0;
    } else if (input == KEY_RIGHT) {
      set_directory(SETDIR_SELECTED);
    } else if (input == KEY_LEFT) {
      set_directory(SETDIR_PARENT);
    }
  }

  return EXIT_SUCCESS;
}

