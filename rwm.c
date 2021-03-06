#include <bits/types/struct_timeval.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include "common.h"

#define trace(...) printf(__VA_ARGS__); fflush(stdout);
#define MAX(A,B) ((A) > (B) ? (A) : (B))

static xcb_connection_t *connection;
static int screen_number;
static xcb_screen_t *screen;

typedef struct Client {
  xcb_window_t window;
  struct Client *next;
} Client;

typedef struct Desktop {
  Client *head;
} Desktop;
static Desktop desktop = { NULL };

static void disconnect() {
  xcb_disconnect(connection);
}

static void die(char* message) {
  trace("%s\n", message);
  disconnect();
  exit(EXIT_FAILURE);
}

static void step(char *step_name, void (*func)(void)) {
  trace("%s...", step_name);
  func();
  trace("ok.\n");
}

static void open_connection() {
  connection = xcb_connect(NULL, &screen_number);
  if (xcb_connection_has_error(connection))  {
    die("Connection error.");
  }
}

static void handle_signal(int signal) {
  trace("Caught signal %d\n", signal);
  disconnect();
  exit(EXIT_SUCCESS);
}

static void trace_desktop(char * message) {
  Client *c = desktop.head;
  int i = 0;
  trace("%s\n", message);
  while (c != NULL) {
    trace("%d - %p\n", ++i, c);
    c=c->next;
  }
}

static bool remove_client(Client *client) {
  // TODO: should not happen;
  if (client == NULL) {
    trace("ERR: client is NULL...");
    return false;
  }
  Client *c, *p;
  for (c = desktop.head; c != client; c=c->next) p=c;
  // TODO: should not happen;
  if (c != client) {
    trace("ERR: could not found client...");
    free(client);
  }
  if (c == desktop.head && c->next == NULL) desktop.head = NULL;
  if (c != NULL && p != NULL) p->next = c->next;
  free(client);
  return true;
}

static Client* create_client(xcb_window_t window) {
  Client *c = malloc(sizeof(Client));
  (*c).window = window;
  (*c).next = NULL;
  return c;
}

static void add_client(Client *client) {
  if (desktop.head == NULL) {
    desktop.head = client;
    return;
  }
  Client *c = desktop.head;
  while (c->next) c = c->next;
  c->next = client;
}

static void setup_signals() {
  signal(SIGINT, handle_signal);
}

static void setup_screens() {
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < screen_number; i++) {
    xcb_screen_next(&iterator);
  }
  screen = iterator.data;
  /* trace("Screen is %dx%d\n", screen->width_in_pixels, screen->height_in_pixels); */
}

static void setup_subscriptions() {
  const static uint32_t values[] = {
    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
      | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
      | XCB_EVENT_MASK_PROPERTY_CHANGE
      /* XCB_EVENT_MASK_EXPOSURE */
      /*   | XCB_EVENT_MASK_KEY_PRESS */
      /*   | XCB_EVENT_MASK_KEY_RELEASE */
      /*   | XCB_EVENT_MASK_BUTTON_PRESS */
      /*   | XCB_EVENT_MASK_BUTTON_RELEASE */
      /*   | XCB_EVENT_MASK_ENTER_WINDOW */
      /*   | XCB_EVENT_MASK_LEAVE_WINDOW */
      /*   | XCB_EVENT_MASK_POINTER_MOTION */
      /*   | XCB_EVENT_MASK_POINTER_MOTION_HINT */
      /*   | XCB_EVENT_MASK_BUTTON_1_MOTION */
      /*   | XCB_EVENT_MASK_BUTTON_2_MOTION */
      /*   | XCB_EVENT_MASK_BUTTON_3_MOTION */
      /*   | XCB_EVENT_MASK_BUTTON_4_MOTION */
      /*   | XCB_EVENT_MASK_BUTTON_5_MOTION */
      /*   | XCB_EVENT_MASK_BUTTON_MOTION */
      /*   | XCB_EVENT_MASK_KEYMAP_STATE */
      /*   | XCB_EVENT_MASK_EXPOSURE */
      /*   | XCB_EVENT_MASK_VISIBILITY_CHANGE */
      /*   | XCB_EVENT_MASK_STRUCTURE_NOTIFY */
      /*   | XCB_EVENT_MASK_RESIZE_REDIRECT */
      /*   | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY */
      /*   | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT */
      /*   | XCB_EVENT_MASK_FOCUS_CHANGE */
      /*   | XCB_EVENT_MASK_PROPERTY_CHANGE */
      /*   | XCB_EVENT_MASK_COLOR_MAP_CHANGE */
      /*   | XCB_EVENT_MASK_OWNER_GRAB_BUTTON */
  };
  xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(connection, screen->root, XCB_CW_EVENT_MASK, values);
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  // TODO: check error code
  if (error) {
    trace("Received error code %d.\n", error->error_code);
    die("Window manager already running.");
  }
  xcb_flush(connection);
  if (xcb_connection_has_error(connection)) {
    die("Connection error after subscribe.");
  }
}

static void setup_windows() {
  xcb_query_tree_reply_t *reply;
  // TODO: check for error
  if ((reply = xcb_query_tree_reply(connection, xcb_query_tree(connection, screen->root), NULL))) {
    xcb_window_t *windows = xcb_query_tree_children(reply);
    int i, len;
    len = xcb_query_tree_children_length(reply);
    for (i = 0; i < len; i++) {
      add_client(create_client(windows[i]));
    }
    trace("found %d...", i);
    xcb_flush(connection);
    free(reply);
  }
}

static void set_window_position(xcb_window_t window, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  uint16_t mask =
    XCB_CONFIG_WINDOW_X
    | XCB_CONFIG_WINDOW_Y
    | XCB_CONFIG_WINDOW_WIDTH
    | XCB_CONFIG_WINDOW_HEIGHT;
  uint32_t values[] = { x, y, w, h };
  xcb_configure_window(connection, window, mask, values);
}

static void tile() {
  int window_count = 0;
  Client *c = desktop.head;
  trace("Tiling...\n");
  while (c != NULL) {
    window_count++;
    c = c->next;
  }
  if (window_count == 0) return;
  uint32_t max_width = screen->width_in_pixels;
  uint32_t max_height = screen->height_in_pixels;
  uint32_t main_width = window_count > 1 ? max_width / 2 : max_width;
  uint32_t stack_width = main_width;
  uint32_t stack_height = window_count > 1 ? max_height / (window_count - 1) : 0;

  int i = 0;
  c = desktop.head;
  while (c != NULL) {
    if (i == 0) {
      trace("Tiling main window at (%d, %d) size (%d, %d).\n", 0, 0, main_width, max_height);
      set_window_position(c->window, 0, 0, main_width, max_height);
    } else {
      trace("Tiling stack window (%d) at (%d, %d) size (%d, %d).\n", (i - 1), main_width, stack_height * (i - 1), stack_width, stack_height);
      set_window_position(c->window, main_width, stack_height * (i - 1), stack_width, stack_height);
    }
    c = c->next;
    i++;
    xcb_flush(connection); // TODO: remove?
  }
  trace("ok(%d).\n", window_count);
}

static void set_fullscreen(xcb_window_t window) {
  // TODO: configure appropriate atoms
  static const uint16_t mask =
    XCB_CONFIG_WINDOW_X
    | XCB_CONFIG_WINDOW_Y
    | XCB_CONFIG_WINDOW_WIDTH
    | XCB_CONFIG_WINDOW_HEIGHT;
  const uint32_t values[] = {
    0,
    0,
    screen->width_in_pixels,
    screen->height_in_pixels,
  };
  xcb_configure_window(connection, window, mask, values);
}

static Client* get_client(xcb_window_t window) {
  Client *c;
  // TODO: search on all desktops
  for (c=desktop.head; c && c->window != window; c=c->next);
  return c;
}

static void map_request(xcb_generic_event_t *event) {
  trace("Handling XCB_MAP_REQUEST...");
  xcb_window_t window = ((xcb_map_request_event_t*)event)->window;
  xcb_map_window(connection, window);
  /* set_fullscreen(window); */
  add_client(create_client(window));
  uint32_t values[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
  xcb_change_window_attributes_checked(connection, window, XCB_CW_EVENT_MASK, values);
  trace("ok.\n");
  tile();
}

static void destroy_notify(xcb_generic_event_t *event) {
  trace("Handling XCB_DESTROY_NOTIFY...");
  xcb_window_t window = ((xcb_destroy_notify_event_t*)event)->window;
  bool removed = remove_client(get_client(window));
  if (removed) tile();
  trace("ok.\n");
}

typedef struct EventHandler {
  uint8_t type;
  void (*func)(xcb_generic_event_t *event);
} EventHandler;

EventHandler event_handlers[] = {
  { XCB_MAP_REQUEST, map_request },
  { XCB_DESTROY_NOTIFY, destroy_notify }
};

static bool handle_event(xcb_generic_event_t *event) {
  EventHandler *handler;
  for (handler = event_handlers; handler->func; handler++) {
    if (handler->type == (event->response_type & ~0x80)) {
      handler->func(event);
      free(event);
      return true;
    }
  }
  return false;
}

static void print_unhandled(xcb_generic_event_t *event) {
  switch (event->response_type & ~0x80) {
    case XCB_EXPOSE:
      trace("Ignoring XCB_EXPOSE.\n"); break;
    case XCB_BUTTON_PRESS:
      trace("Ignoring XCB_BUTTON_PRESS.\n"); break;
    case XCB_CONFIGURE_REQUEST:
      trace("Ignoring XCB_CONFIGURE_REQUEST.\n"); break;
    case XCB_CLIENT_MESSAGE:
      trace("Ignoring XCB_CLIENT_MESSAGE.\n"); break;
    case XCB_CREATE_NOTIFY:
      trace("Ignoring XCB_CREATE_NOTIFY.\n"); break;
    case XCB_CONFIGURE_NOTIFY:
      trace("Ignoring XCB_CONFIGURE_NOTIFY.\n"); break;
    case XCB_MAP_NOTIFY:
      trace("Ignoring XCB_MAP_NOTIFY.\n"); break;
    case XCB_UNMAP_NOTIFY:
      trace("Ignoring XCB_UNMAP_NOTIFY.\n"); break;
    case XCB_PROPERTY_NOTIFY:
      trace("Ignoring XCB_PROPERTY_NOTIFY.\n"); break;
    default:
      trace("Ignoring UNKNOWN event %d\n", event->response_type & ~0x80); break;
  }
}

bool running = true;
static void event_loop() {
  unlink(RWM_SOCK_PATH);

  struct sockaddr_un socket_address;
  if (strlen(RWM_SOCK_PATH) > sizeof(socket_address.sun_path) - 1) {
    die("Socket path too long");
  }

  if (remove(RWM_SOCK_PATH) < 0 && errno != ENOENT) {
    die("Could not remove socket file");
  }

  memset(&socket_address, 0, sizeof(struct sockaddr_un));
  socket_address.sun_family = AF_UNIX;
  strncpy(socket_address.sun_path, RWM_SOCK_PATH, sizeof(socket_address.sun_path) - 1);

  int display_fd = xcb_get_file_descriptor(connection);
  int socket_fd = 0;

  if ((socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    die("Could not create socket\n.");
  }
  if (bind(socket_fd, (struct sockaddr*)&socket_address, sizeof(struct sockaddr_un)) < 0) {
    die("Could not bind socket\n.");
  }
  if (listen(socket_fd, BACKLOG) < 0) {
    die("Could not listen to socket.\n");
  }

  fd_set active_fd_set, read_fd_set;
  FD_ZERO(&active_fd_set);
  FD_SET(socket_fd, &active_fd_set);
  FD_SET(display_fd, &active_fd_set);

  int max_fd = MAX(socket_fd, display_fd);
  xcb_generic_event_t *event;
  do {
    read_fd_set = active_fd_set;
    if (select(max_fd + 1, &read_fd_set, NULL, NULL, NULL) < 0) {
      die("Could not select.\n");
    }

    for (int fd = 0; fd <= max_fd; fd++) {
      if (FD_ISSET(fd, &read_fd_set)) {
        if (fd == socket_fd) {
          trace("Checking for new connections...\n");
          int client_fd;
          if ((client_fd = accept(socket_fd, NULL, 0)) < 0) {
            die("Could not accept(%d).\n");
          }
          trace("Reading from client...\n");
          char buffer[BUFFER_SIZE];
          int read_bytes = read(client_fd, buffer, BUFFER_SIZE);
          if (read_bytes < 0) {
            trace("Could not read.\n");
          } else if (read_bytes > 0) {
            trace("Received '%s' from(%d).\n", buffer, client_fd);
            if (strcmp(buffer, "quit") == 0) {
              char* reply = "quiting";
              if (send(client_fd, reply, strlen(reply), 0) != strlen(reply)) {
                trace("Partial message sent.\n");
              }
              running = false;
            }
          }
          if (close(client_fd) < 0) {
            trace("Could not close (%d).\n", client_fd);
          }
        } else if (fd == display_fd) {
          while ((event = xcb_poll_for_event(connection))) {
            if (handle_event(event)) continue;
            print_unhandled(event);
            free(event);
          }
        }
      }
    }

    xcb_flush(connection);
  } while (running);
}

int main() {
  trace("Initializing rwm...\n");
  step("Connection", open_connection);
  step("Signaling", setup_signals);
  step("Screens", setup_screens);
  step("Windows", setup_windows);
  step("Subscribing", setup_subscriptions);
  event_loop();
  disconnect();
  exit(EXIT_SUCCESS);
}
