#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <libgen.h>
#include <getopt.h>
#include <paths.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAXLINE 4096

static Display *display = NULL;
static Window window;
static Time timestamp;
static Atom clipboard_atom;
static Atom targets_atom;
static Atom property_atom;
static Atom null_atom;
static Atom uri_list_atom;
static Atom copied_files_atom;
static Atom utf8_string_atom;
static Atom text_atom;

static char opts[] = "hcyp";
static struct option long_options[] = {
  {"help", 1, 0, 'h'},
  {"copy", 1, 0, 'c'},
  {"yank", 1, 0, 'y'},
  {"paste", 1, 0, 'p'},
  {0, 0, 0, 0}
};
static int command = 0;
static size_t file_count;
static char **files = NULL;
static char *dest = NULL;

static void
print_help(char *cmd)
{
  cmd = basename(cmd);
  printf("Usage: %s [PARAMS...] [FILE...]\n", cmd);
  printf("Params: \n  -h, --help    Print command line options.\n");
  printf("  -c, --copy    Copy files to clipboard\n");
  printf("  -y, --cut     Cut files to clipboard\n");
  printf("  -p, --paste   Paste files from clipboard to directory\n");
}

static size_t
build_uri_list(char *uri_list, char **files, size_t file_count)
{
  size_t uri_list_size = 0;
  char rpath[PATH_MAX];
  for (size_t i=0; i<file_count; i++)
    {
      if (realpath(files[i], rpath) != rpath)
        {
          fprintf(stderr, "Bad file name: '%s'", files[i]);
          exit(1);
        }
      size_t len = strlen(rpath);
      if (uri_list)
        sprintf(uri_list + uri_list_size, "file://%s\n", rpath);
      uri_list_size += len + 8;
    }

  return uri_list_size-1;
}

static size_t
build_text(char *uri_list, char delim, char **files, size_t file_count) {
  size_t text_size = 0;
  char rpath[PATH_MAX];
  for (size_t i=0; i<file_count; i++)
    {
      if (realpath(files[i], rpath) != rpath)
        {
          fprintf(stderr, "Bad file name: '%s'", files[i]);
          exit(1);
        }
      size_t len = strlen(rpath);
      if (uri_list)
        sprintf(uri_list + text_size, "%s%c", rpath, delim);
      text_size += len + 1;
    }
  return text_size-1;
}

static Time
get_timestamp (void)
{
  XEvent event;
  XChangeProperty (display, window, XA_WM_NAME, XA_STRING, 8,
                   PropModeAppend, NULL, 0);
  while (1) {
    XNextEvent (display, &event);
    if (event.type == PropertyNotify)
      return event.xproperty.time;
  }
}

static unsigned char *
wait_selection (Atom selection, Atom request_target)
{
  XEvent event;
  Bool keep_waiting = True;
  Atom target;
  int format;
  unsigned long bytesafter, length;
  unsigned char * value, * retval = NULL;

  while (keep_waiting)
    {
      XNextEvent (display, &event);
      switch (event.type)
        {
        case SelectionNotify:
            if (event.xselection.selection != selection) break;
            if (event.xselection.property != None)
              {
                XGetWindowProperty (event.xselection.display,
                                    event.xselection.requestor,
                                    event.xselection.property, 0L, 1000000,
                                    False, (Atom)AnyPropertyType, &target,
                                    &format, &length, &bytesafter, &value);
                if (target == request_target)
                    retval = (unsigned char *) strdup ((char *) value);
                XFree (value);
                XDeleteProperty (event.xselection.display,
                                 event.xselection.requestor,
                                 event.xselection.property);
              }
            keep_waiting = False;
          break;
        default:
          break;
        }
    }
  return retval;
}

static char *
get_selection (Atom selection, Atom request_target)
{
  unsigned char * retval;
  XConvertSelection (display, selection, request_target, property_atom, window, timestamp);
  XSync (display, False);
  retval = wait_selection (selection, request_target);
  return (char *)retval;
}

static int
handle_x_errors (Display * display, XErrorEvent * eev)
{
  char err_buf[MAXLINE];
  XGetErrorText (display, eev->error_code, err_buf, MAXLINE);
  fprintf(stderr, "%s\n", err_buf);
  exit(1);
}

static void
wait_selection_requests()
{
  Window owner;
  XSetSelectionOwner (display, clipboard_atom, window, timestamp);
  owner = XGetSelectionOwner (display, clipboard_atom);
  if (owner == window)
    {
      XSetErrorHandler (handle_x_errors);
      for (;;)
        {
          XEvent event;
          XFlush (display);
          XNextEvent (display, &event);
          switch (event.type) {
            case SelectionClear:
              if (event.xselectionclear.selection == clipboard_atom) return;
              break;
            case SelectionRequest:
              if (event.xselectionrequest.selection != clipboard_atom) break;
              XSelectionRequestEvent * xsr = &event.xselectionrequest;
              XSelectionEvent ev;

              ev.type = SelectionNotify;
              ev.display = xsr->display;
              ev.requestor = xsr->requestor;
              ev.selection = xsr->selection;
              ev.time = xsr->time;
              ev.target = xsr->target;

              if (ev.time != CurrentTime && ev.time < timestamp)
                {
                  ev.property = None;
                }
              else if (ev.target == targets_atom)
                {
                  Atom types[] = { targets_atom, uri_list_atom, copied_files_atom, utf8_string_atom, text_atom };
                  ev.property = xsr->property;
                  XChangeProperty(display, ev.requestor, ev.property, XA_ATOM,
                                  32, PropModeReplace, (unsigned char *) types,
                                  (int) (sizeof(types) / sizeof(Atom)));
                }
              else if (ev.target == uri_list_atom)
                {
                  char *uri_list;
                  size_t uri_list_len = 0;

                  uri_list_len = build_uri_list(NULL, files, file_count);
                  uri_list = alloca(uri_list_len+1);
                  build_uri_list(uri_list, files, file_count);
                  uri_list[uri_list_len] = '\0';

                  ev.property = xsr->property;
                  XChangeProperty (display, ev.requestor, ev.property, ev.target,
                                   8, PropModeReplace, (unsigned char *) uri_list,
                                   (int) uri_list_len);
                }
              else if (ev.target == copied_files_atom)
                {
                  char *uri_list;
                  size_t uri_list_len = 0;
                  if (command == 'y')
                      uri_list = strdup("cut\n");
                  else if (command == 'c')
                      uri_list = strdup("copy\n");
                  else
                    {
                      fprintf(stderr, "Error command\n");
                      exit(1);
                    }

                  uri_list_len = strlen(uri_list) + build_uri_list(NULL, files, file_count);
                  uri_list = realloc(uri_list, uri_list_len+1);
                  build_uri_list(uri_list + strlen(uri_list), files, file_count);
                  uri_list[uri_list_len] = '\0';

                  ev.property = xsr->property;
                  XChangeProperty (display, ev.requestor, ev.property, ev.target,
                                   8, PropModeReplace, (unsigned char *) uri_list,
                                   (int) uri_list_len);
                  free(uri_list);
                }
              else if (ev.target == utf8_string_atom || ev.target == text_atom)
                {
                  char *string;
                  size_t string_len = build_text(NULL, ' ', files, file_count);
                  string = alloca(string_len+1);
                  if (ev.target == utf8_string_atom)
                    build_text(string, ' ', files, file_count);
                  else
                    build_text(string, '\n', files, file_count);
                  string[string_len] = '\0';

                  ev.property = xsr->property;
                  XChangeProperty (display, ev.requestor, ev.property, ev.target,
                                   8, PropModeReplace, (unsigned char *) string,
                                   (int) string_len);
                }
              else
                ev.property = None;

              XSendEvent (display, ev.requestor, False,
                          (unsigned long)NULL, (XEvent *)&ev);

              break;
            default:
              break;
            }
          XSync (display, False);
        }
    }
}

static size_t
files_from_uri_list(char **arr,
                    char *list)
{
  size_t res = 0;
  while ((list = strchr(list, '\n')))
    {
      if (arr)
        {
          *list = '\0';
          list ++;
          *arr = list;
          arr ++;
        }
      else
        list ++;
      res ++;
    }
  return res;
}

static int
do_paste()
{
  char *uri_list = get_selection(clipboard_atom, copied_files_atom);
  if (!uri_list)
    return 1;
  const char *prefix = "file://";
  const int prefix_len = 7;
  file_count = 0;
  file_count = files_from_uri_list(NULL, uri_list);
  if (file_count <= 0)
    return True;
  files = alloca((size_t) (sizeof (char*) * file_count));
  files_from_uri_list(files, uri_list);
  if (file_count <= 0)
    {
      fprintf(stderr, "no files\n");
      if (uri_list)
        free(uri_list);
      return 1;
    }

  for (size_t i=0; i<file_count; i++)
    {
      if (files[i][0] == '\n' ||
          files[i][0] == '\0' ||
          strncmp(files[i], prefix, prefix_len) != 0)
        {
          fprintf(stderr, "not supported item: %s\n", files[i]);
          if (uri_list)
            free(uri_list);
          return 1;
        }
      files[i] += prefix_len;
    }

  char **args = alloca(file_count+4);
  int argn = 0;

  if (strncmp("cut", uri_list, 3) == 0)
    args[argn++] = "/bin/mv";
  else if (strncmp("copy", uri_list, 4) == 0)
    {
      args[argn++] = "/bin/cp";
      args[argn++] = "-r";
    }
  else
    {
      fprintf(stderr, "not supported operation\n");
      if (uri_list)
        free(uri_list);
      return 1;
    }

  for (size_t i=0; i<file_count; i++)
    args[argn++] = files[i];
  args[argn++] = dest;
  args[argn++] = NULL;
  if (uri_list)
    free(uri_list);
  return execv(args[0], args);
}

static void
before_exit()
{
  if (display)
    XCloseDisplay(display);
  if (files)
    {
      for (size_t i = 0; i<file_count; i++)
        free(files[i]);
      free(files);
      files = NULL;
    }
}

int main(int argc, char **argv)
{
  int c;
  int option_index = 0;

  atexit( before_exit );

  while (1)
    {
      c = getopt_long (argc, argv, opts,
                       long_options, &option_index);
      if (c == -1)
        break;

      switch (c)
        {
          case 'h':
            print_help(argv[0]);
            return 0;
          case 'c':
          case 'y':
          case 'p':
            if (command)
              {
                fprintf(stderr, "Only one option!\n");
                return 1;
              }
            command = c;
          break;
          default:
            print_help(argv[0]);
            return 0;
        }
    }

  if (command == 'c' || command == 'y')
    {
      file_count = (size_t) (argc - optind);
      if (file_count <= 0)
        {
          fprintf(stderr, "Missing operand specifying file!\n");
          exit(1);
        }
      char **argfiles = argv + optind;
      char rpath[PATH_MAX];
      files = malloc(sizeof (char*) * file_count);
      for (size_t i=0; i<file_count; i++)
        {
          if (realpath(argfiles[i], rpath) != rpath)
            {
              fprintf(stderr, "Bad file name: '%s'", argfiles[i]);
              exit(1);
            }
          files[i] = strdup(rpath);
        }
    }
  else if (command == 'p')
    {
      if (argc - optind > 1)
        {
          fprintf(stderr, "Too many arguments!\n");
          exit(1);
        }
      else if (argc - optind == 1)
        dest = argv[optind];
      else
        dest = "./";

    }
  else if (command != 'p')
    {
      fprintf(stderr, "Missing operand specifying command!\n");
      exit(1);
    }

  display = XOpenDisplay(getenv("DISPLAY"));
  if (display == NULL)
    {
      fprintf(stderr, "Cannot open display\n");
      return 1;
    }

  Window root = XDefaultRootWindow (display);
  unsigned long black = BlackPixel (display, DefaultScreen (display));
  window = XCreateSimpleWindow (display, root, 0, 0, 1, 1, 0, black, black);
  XSelectInput (display, window, PropertyChangeMask);
  timestamp = get_timestamp ();
  clipboard_atom = XInternAtom (display, "CLIPBOARD", False);
  targets_atom = XInternAtom (display, "TARGETS", False);
  property_atom = XInternAtom(display, "XSEL_DATA", False);
  null_atom = XInternAtom (display, "NULL", False);
  uri_list_atom = XInternAtom (display, "text/uri-list", False);
  copied_files_atom = XInternAtom (display, "x-special/gnome-copied-files", False);
  utf8_string_atom = XInternAtom (display, "UTF8_STRING", False);
  text_atom = XInternAtom (display, "TEXT", False);

  if (command == 'p')
    {
      return do_paste();
    }
  else if (command == 'y' || command == 'c')
    {
      pid_t pid;
      if ((pid = fork()) == -1) {
        fprintf(stderr, "error forking");
        return 1;
      } else if (pid > 0) {
        _exit(0);
      }

      umask(0);
      setsid();
      chdir("/");
      close(STDIN_FILENO);
      close(STDOUT_FILENO);
      close(STDERR_FILENO);

      wait_selection_requests();
    }


  XCloseDisplay(display);
  display = NULL;
  if (files)
    {
      for (size_t i = 0; i<file_count; i++)
        free(files[i]);
      free(files);
      files = NULL;
    }

  return 0;
}
