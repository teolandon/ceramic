/* Include statements */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* Defines */

#define CERAMIC_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

/* Structs
 *
 **
 ** termios for terminal attributes
 **
 *
 */

struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

/* Terminal sets */

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  
  perror(s);
  exit(1);
}

void disableRawMode() {
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

/* Read keys */

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die ("read");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
      return '\x1b';
    }
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      die("uuuuuu");
      return '\x1b';
    }

    if (seq[0] == '[') {
      die("lolololol");
      switch (seq[1]) {
        case 'A': return 'h';
        case 'B': return 'l';
        case 'C': return 'j';
        case 'D': return 'k';
      }
    }

    return '\x1b';
  }
  else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    return -1;
  }
  
  while(i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
      i++;
  }
  
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;
  
  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    printf("still not broken\r\n");
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
      printf("about to happen\r\n");
      return -1;
    }

    return getCursorPosition(rows, cols);
  }
  else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/* Append Buffer */

struct abuf {
  char *b;
  int length;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int length) {
  char *new = realloc(ab->b, ab->length + length);
  memcpy(&new[ab->length], s, length);
  ab->b = new;
  ab->length += length;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/* Output */

void editorDrawRows(struct abuf *ab) {
  int i;

  for (i=0; i < E.screenrows - 1; i++) {
    if (i == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
        "Ceramic text editor -- version %s", CERAMIC_VERSION);
      if (welcomelen > E.screencols) 
        welcomelen = E.screencols;

      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }

      while (padding--)
        abAppend(ab, " ", 1);

      abAppend(ab, welcome, welcomelen);
      abAppend(ab, "\r\n", 2);
    }
    else {
      abAppend(ab, "~\x1b[K\r\n", 6);
    }
  }
  abAppend(ab, "~\x1b[K", 4);
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  //abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  //abAppend(&ab, "\x1b[?25h", 3);

  write(STDOUT_FILENO, ab.b, ab.length);
  abFree(&ab);
}

/* Input Process */

void editorMoveCursor(char key) {
  switch (key) {
    case 'h':
      E.cx--;
      break;
    case 'l':
      E.cx++;
      break;
    case 'j':
      E.cy++;
      break;
    case 'k':
      E.cy--;
      break;
  }
}

void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    
    // Arrow keys
    case 'h':
    case 'j':
    case 'k':
    case 'l':
      editorMoveCursor(c);
      break;
  }
}

/* Initialization */

void initEditor() {
  E.cx = 0;
  E.cy = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSizzzz");
}

/* Main */

int main() {

  enableRawMode();
  initEditor();
  
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;

}
