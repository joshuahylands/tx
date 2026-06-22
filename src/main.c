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

#define KEY_UP -2
#define KEY_DOWN -3
#define KEY_RIGHT -4
#define KEY_LEFT -5

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
#define STYLE_RESET ESC("0m")
#define STYLE_BOLD ESC("1m")
#define STYLE_UNDERLINE ESC("4m")

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

void get_terminal_size(int _) {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0) {
    exit(EXIT_FAILURE);
  }

  g_terminal.lines = w.ws_row;
  g_terminal.columns = w.ws_col;
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

  // setup terminal
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
    exit(EXIT_FAILURE);
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
    exit(EXIT_FAILURE);
  }

  // get entries
  g_directory.entries_length = scandir(
    g_directory.path,
    &g_directory.entries,
    NULL,
    alphasort
  );
  if (g_directory.entries_length == -1) {
    exit(EXIT_FAILURE);
  }

  // get status of each file
  g_directory.entries_stat = malloc(sizeof(struct stat) * g_directory.entries_length);
  if (g_directory.entries_stat == NULL) {
    exit(EXIT_FAILURE);
  }

  for (int i = 0; i < g_directory.entries_length; i++) {
    if (stat(g_directory.entries[i]->d_name, &(g_directory.entries_stat[i])) == -1) {
      exit(EXIT_FAILURE);
    };
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
    exit(EXIT_FAILURE);
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
        STYLE_BOLD "%s "
        STYLE_UNDERLINE "%s >"
        STYLE_RESET "\n",

        fileType,
        g_directory.entries[i]->d_name
      );
    } else {
      printf(
        STYLE_BOLD "%s "
        STYLE_RESET "%s\n",

        fileType,
        g_directory.entries[i]->d_name
      );
    }
  }
}

int get_input(void) {
  int input = getchar();

  if (input == '\x1b') {
    input = getchar();

    if (input == '[') {
      input = getchar();

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
    }
  }

  return input;
}

int main(void) {
  init_terminal();
  get_directory();
  atexit(cleanup_directory);

  bool running = true;
  while (running) {
    printf(DELETE_ALL SET_CURSOR_HOME);
    display_directory();
    fflush(stdout);

    int input = get_input();

    if (input == 'q') {
      running = false;
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

