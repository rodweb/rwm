#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <xcb/xcb.h>

#define trace(...) printf(__VA_ARGS__); fflush(stdout);

static xcb_connection_t *connection;
static int screenNumber;
static xcb_screen_t *screen;

typedef struct Client {
  xcb_window_t window;
  struct Client *next;
} Client;

typedef struct {
  Client *head;
} Desktop;
static Desktop desktop = { NULL };

static void disconnect() {
  xcb_disconnect(connection);
}

static void die(char* message) {
  trace("%s\n", message);
  disconnect();
  exit(1);
}

static void step(char *step_name, void (*func)(void)) {
  trace("%s...", step_name);
  func();
  trace("ok.\n");
}

static void connect() {
  connection = xcb_connect(NULL, &screenNumber);
  if (xcb_connection_has_error(connection))  {
    die("Connection error.");
  }
}

static void handle_signal(int signal) {
  trace("Caught signal %d\n", signal);
  disconnect();
  exit(0);
}

static void remove_client(Client *client) {
  // TODO: should not happen;
  if (client == NULL) return;
  Client *c, *p;
  for (c = desktop.head; c != client; c=c->next) p=c;
  if (p) p->next = c->next;
  if (c == desktop.head && c->next == NULL) desktop.head = NULL;
  free(client);
}

static Client* create_client(xcb_window_t window) {
  Client *c = malloc(sizeof(Client));
  (*c).window = window;
  (*c).next = NULL;
  return c;
}

static void add_client(Client *client) {
  Client *c = desktop.head;
  if (desktop.head == NULL) {
    desktop.head = client;
  } else {
    while (c->next != NULL) {
      c = c->next;
    }
    c->next = client;
  }
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

static void setup_signals() {
  signal(SIGINT, handle_signal);
}

static void setup_screens() {
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < screenNumber; i++) {
    xcb_screen_next(&iterator);
  }
  screen = iterator.data;
  /* trace("Screen is %dx%d\n", screen->width_in_pixels, screen->height_in_pixels); */
}

static void setup_subscriptions() {
  const static uint32_t values[] = {
    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
      | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
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
    int i;
    for (i = 0; i < xcb_query_tree_children_length(reply); i++) {
      add_client(create_client(windows[i]));
    }
    trace("Found %d windows...", i + 1);
    free(reply);
  }
}

static void tile() {
  int totalClients = 0;
  Client *c = desktop.head;
  trace("Tiling...");
  while (c != NULL) {
    totalClients++;
    c = c->next;
  }
  trace("ok(%d).\n", totalClients);
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
  set_fullscreen(window);
  add_client(create_client(window));
  trace("ok.\n");
  tile();
}

static void destroy_notify(xcb_generic_event_t *event) {
  trace("Handling XCB_DESTROY_NOTIFY...");
  xcb_window_t window = ((xcb_destroy_notify_event_t*)event)->window;
  remove_client(get_client(window));
  trace("ok.\n");
  tile();
}

typedef struct {
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
      return true;
    }
  }
  return false;
}

static void event_loop() {
  xcb_generic_event_t *event;
  // TODO: switch to non-blocking xcb_poll_for_event
  while ((event = xcb_wait_for_event(connection))) {
    if (handle_event(event)) {
      xcb_flush(connection);
      free(event);
      continue;
    }
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
      default:
        trace("Ignoring UNKNOWN event %d\n", event->response_type & ~0x80); break;
    }
    xcb_flush(connection);
    free(event);
  }
}

int main() {
  trace("Initializing rwm...\n");
  step("Connection", connect);
  step("Signaling", setup_signals);
  step("Screens", setup_screens);
  step("Windows", setup_windows);
  step("Subscribing", setup_subscriptions);
  event_loop();
  disconnect();
  return 0;
}
