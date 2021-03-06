/* Include statements */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <time.h>
#include <unistd.h>

/* Defines */

#define CERAMIC_VERSION "0.0.1"
#define CERAMIC_TAB_STOP 8
#define CERAMIC_QUIT_TIMES 2

#define CTRL_KEY(k) ((k) & 0x1f)


/* Keys enum */
enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY,
  DELETE_KEY
};

enum modes {
  NORMAL,
  INSERT,
  VISUAL,
  TEST
};

/* Doc: struct erow
 ----------------------------------------------
 * A single row in the open buffer
 *
 * int size:
 *
 * * the number of characters in the row
 * * size + 1 = size of the array
 *
 * int rsize:
 *
 * * the length of the row in character widths
 * * Tabs = 8 spaces, so 8 character widths
 * * * Insert all such examples here
 *
 * char *chars:
 *
 * * character array, with length size+1
 * * 0 terminated
 * * contains all characters in row
 *
 * char *render:
 *
 * * character array, with length rsize+1
 * * 0 terminated
 * * contains all characters to be rendered
 *
 ----------------------------------------------*/
typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

/* Doc: struct editorConfig
 ----------------------------------------------
 * Current configuration of an editor
 *
 * int cx, cy:
 *
 * * current position of the cursor in terms of
 * *     rows and characters in row
 *
 * int rx:
 *
 * * currect horizontal position of the cursor
 * *     in terms of rendered characters (see
 * *     Doc: _______ for more info)
 *
 * int rowoff, coloff:
 *
 * * row and coloumn offset in display from
 * *     beginning of file, used for scrolling
 *
 * int screenrows, screencols, numrows:
 *
 * * current total screen rows and columns,
 * *     and total number of rows in file
 *
 * erow *row:
 *
 * * dynamically allocated array of rows in file
 * * size = numrows
 *
 * int dirty:
 *
 * * indicates whether the open file has been
 * *     modified or not
 * * dirty = 0 when file hasn't been modified
 * * dirty > 0 when file has been modified
 *
 * char *filename:
 *
 * * string of the filename of open file
 * * when NULL, editor works, but prompts for
 * *     filename on save
 *
 * char statusmsg[80]:
 *
 * * current status message, displayed on
 * *     statusbar
 * * maximum of 80 [Convert to a constant]
 *
 * time_t statusmsg_time:
 *
 * * time when statusmsg was set, used to
 * *     calculate when the message expires
 *
 * struct termios orig_termios:
 *
 * * termios struct used to set terminal
 * *     values
 *
 ----------------------------------------------*/
struct editorConfig {
  int mode;

  int cx, cy;
  int rx;

  int rowoff;
  int coloff;

  int screenrows;
  int screencols;
  int numrows;

  int r_mov;

  erow *row;

  int dirty;

  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;

  struct termios orig_termios;
};

struct editorConfig E;

/* Function Declarations */

void editorSetStatusMessage(const char *fmt, ...);
void editorClearStatusMessage();
void editorRefreshScreen();
char *editorPrompt(char *prompt, void(*callback)(char *, int));
int editorRowRxToCx(erow *row, int rx);

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

int editorReadKey() {
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
      return '\x1b';
    }

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) {
          return '\x1b';
        }
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DELETE_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      }
      else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    }
    else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
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
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
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

/* row operations */

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (CERAMIC_TAB_STOP - 1) - (rx % CERAMIC_TAB_STOP);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (CERAMIC_TAB_STOP - 1) - (cur_rx % CERAMIC_TAB_STOP);
    cur_rx++;

    if (cur_rx > rx)
      return cx;
  }
  return cx;
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for(j = 0; j < row->size; j++)
    if (row->chars[j] == '\t')
      tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs*(CERAMIC_TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % CERAMIC_TAB_STOP != 0)
        row->render[idx++] = ' ';
    }
    else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
    row->rsize = idx;
}

void editorInsertRow (int i, char *s, size_t length) {
  if (i < 0 || i > E.numrows)
    return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[i+1], &E.row[i], sizeof(erow) * (E.numrows - i));

  E.row[i].size = length;
  E.row[i].chars = malloc(length + 1);
  memcpy(E.row[i].chars, s, length);
  E.row[i].chars[length] = '\0';

  E.row[i].rsize= 0;
  E.row[i].render = NULL;
  editorUpdateRow(&E.row[i]);

  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void editorDeleteRow(int i) {
  if (i < 0 || i >= E.numrows)
    return;
  editorFreeRow(&E.row[i]);
  memmove(&E.row[i], &E.row[i+1], sizeof(erow) * (E.numrows - i - 1));
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int i, int c) {
  if (i < 0 || i > row->size)
    i = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[i+1] ,&row->chars[i], row->size - i + 1);
  row->size++;
  row->chars[i] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorInsertNewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  }
  else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDeleteChar(erow *row, int i) {
  if (i < 0 || i >= row->size)
    return;
  memmove(&row->chars[i], &row->chars[i+1], row->size - i);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/* Editor Operations */

void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow (E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorDeleteChar() {
  if (E.cy == E.numrows || (E.cx == 0 && E.cy == 0)) {
    return;
  }
  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDeleteChar(row, --(E.cx));
  }
  else {
    E.cx = E.row[--E.cy].size;
    editorRowAppendString(&E.row[E.cy], row->chars, row->size);
    editorDeleteRow(E.cy+1);
  }
}

/* File I/O */

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++) {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;

  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  // buf has to be freed
  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if(E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s", NULL);
    if(E.filename == NULL) {
      editorSetStatusMessage("Save canceled");
      return;
    }
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);

  //Write file out, checking for errors
  if(fd != 1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        editorSetStatusMessage("%d bytes written to %.20s", len, E.filename);
        E.dirty = 0;
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O Error: %s", strerror(errno));
}

void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  }
  else if (key == ARROW_RIGHT) {
    direction = 1;
  }
  else if (key == ARROW_LEFT) {
    direction = 1;
  }
  else {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1)
    direction = 1;
  int curr = last_match;

  int i;
  for (i = 0; i < E.numrows; i++) {
    curr += direction;
    if (curr == -1)
      curr = E.numrows - 1;
    else if (curr == E.numrows)
      curr = 0;

    erow *row = &E.row[curr];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = curr;
      E.cy = curr;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;
      break;
    }
  }
}

void editorFind() {
  int saved_c_x = E.cx;
  int saved_c_y = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;
  char *query = editorPrompt("Search: %s (ESC to cancel)", editorFindCallback);

  if (query)
    free(query);
  else {
    E.cx = saved_c_x;
    E.cy = saved_c_y;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
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

void editorScroll() {
  if (E.cy < E.numrows && E.r_mov) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorDrawRows(struct abuf *ab) {
  int i;
  for (i=0; i < E.screenrows; i++) {
    int filerow = i + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && i == E.screenrows / 3) {
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
      }
      else {
        abAppend(ab, "~", 1);
      }
    }
    else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }

    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
      E.filename ? E.filename : "[No file]", E.numrows,
      E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
      E.cy + 1, E.numrows);
  if (len > E.screencols)
    len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    }
    else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  int rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                            (rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.length);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void editorClearStatusMessage() {
  editorSetStatusMessage("");
}

/* Input Process */

char *editorPrompt(char *prompt, void(*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while(1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if ((c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
        && buflen != 0) {
      buf[--buflen] = '\0';
    }
    else if (c == '\x1b') {
      editorClearStatusMessage();
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    }
    if (c == '\r') {
      if (buflen != 0) {
        editorClearStatusMessage();
        if (callback) callback(buf, c);
        return buf;
      }
    }
    else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback) callback(buf, c);
  }
}

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (E.cx != 0) {
        E.cx--;
      }
      else if (E.cy > 0 && E.mode == INSERT) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      E.r_mov = 1;
      break;
    case ARROW_RIGHT:
    case 'l':
      if (row && E.cx < row->size) {
        E.cx++;
      }
      else if (row && E.cx == row->size && E.mode == INSERT) {
        E.cy++;
        E.cx = 0;
      }
      E.r_mov = 1;
      break;
    case ARROW_UP:
    case 'k':
      if (E.cy != 0) {
        E.cy--;
        E.cx = editorRowRxToCx(&E.row[E.cy], E.rx);
        E.r_mov = 0;
      }
      else if (E.mode == INSERT){
        E.cx = 0;
        E.r_mov = 1;
      }
      break;
    case ARROW_DOWN:
    case 'j':
      if (E.cy < E.numrows) {
        E.cy++;
        E.cx = editorRowRxToCx(&E.row[E.cy], E.rx);
        E.r_mov = 0;
      }
      break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx >= rowlen) {
    E.cx = (E.mode == NORMAL) ? rowlen - 1 : rowlen;
  }
}

void editorProcessKeypress() {
  static int quit_times = CERAMIC_QUIT_TIMES;
  int c = editorReadKey();

  // Clear Statusbar from modified file warning message
  editorClearStatusMessage();

  // Universal hotkeys
  switch (c) {
    case CTRL_KEY('q'):
      if(E.dirty && --quit_times) {
        editorSetStatusMessage("Warning: File has been modified. "
            "Press Ctrl-Q to exit without saving changes.");
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    // Page up-down

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case CTRL_KEY('f'):
      editorFind();
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if(c == PAGE_UP)
          E.cy = E.rowoff;
        else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows)
           E.cy = E.numrows;
        }
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }

  }

  // Mode switch statement
  switch (E.mode) {

    case INSERT:
      switch (c) {
        case '\r':
          editorInsertNewline();
          break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DELETE_KEY:
          if (c == DELETE_KEY)
            editorMoveCursor(ARROW_RIGHT);
          editorDeleteChar();
          break;

        // Arrow keys
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
        case ARROW_DOWN:
          editorMoveCursor(c);
          break;

        case CTRL_KEY('l'):
        case '\x1b':
          E.mode = NORMAL;
          break;

        default:
          editorInsertChar(c);
          break;
      }
      break;

    case NORMAL:
      switch(c) {
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          editorMoveCursor(c);
          break;
        case 'i':
          E.mode = INSERT;
          break;
        case CTRL_KEY('q'):
          if(E.dirty && --quit_times) {
            editorSetStatusMessage("Warning: File has been modified. "
                "Press Ctrl-Q to exit without saving changes.");
            return;
          }
          write(STDOUT_FILENO, "\x1b[2J", 4);
          write(STDOUT_FILENO, "\x1b[H", 3);
          exit(0);
          break;
      }
      break;
  }

  quit_times = CERAMIC_QUIT_TIMES;
}

/* Initialization */

void initEditor() {
  E.mode = NORMAL;

  E.cx = 0;
  E.cy = 0;
  E.rx = 0;

  E.rowoff = 0;
  E.coloff = 0;

  E.r_mov = 0;

  E.numrows=0;
  E.row = NULL;
  E.dirty = 0;

  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2;
  E.filename = NULL;
}

/* Main */

int main(int argc, char*argv[]) {

  enableRawMode();
  initEditor();
  if(argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-S: Save | Ctrl-Q: Quit | Ctrl-F: Find");

  while(1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
}
