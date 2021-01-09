#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <xcb/xcb.h>

static xcb_connection_t *connection;
static int screenNumber;
static xcb_screen_t *screen;

static void disconnect() {
  xcb_disconnect(connection);
}

static void die(char* message) {
  printf("%s\n", message);
  disconnect();
  exit(1);
}

static void connect() {
  connection = xcb_connect(NULL, &screenNumber);
  if (xcb_connection_has_error(connection))  {
    die("Connection error.");
  }
  printf("Current screen is %d\n", screenNumber);
}

static void handle_signal(int signal) {
  printf("Caught signal %d\n", signal);
  disconnect();
  exit(0);
}

static void signals() {
  signal(SIGINT, handle_signal);
}

static void subscribe() {
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
    printf("Received error code %d.\n", error->error_code);
    die("Window manager already running.");
  }
  xcb_flush(connection);
  if (xcb_connection_has_error(connection)) {
    die("Connection error after subscribe.");
  }
}

static void setup() {
  signals();
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < screenNumber; i++) {
    xcb_screen_next(&iterator);
  }
  screen = iterator.data;
  printf("Screen is %dx%d\n", screen->width_in_pixels, screen->height_in_pixels);
  subscribe();
}

typedef struct Client {
  xcb_window_t window;
  struct Client *next;
} Client;

typedef struct {
  Client *head;
} Desktop;

static Desktop desktop = { NULL };

static void tile() {
  int totalClients = 0;
  Client *c = desktop.head;
  printf("Counting\n");
  fflush(stdout);
  while (c != NULL) {
    printf("i = %d, %p\n", totalClients, c);
    fflush(stdout);
    totalClients++;
    c = c->next;
  }
  printf("Total clients %d\n", totalClients);
  fflush(stdout);
}

static void fullscreen(xcb_window_t window) {
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
  Client *c = desktop.head;
  // TODO: search on all desktops
  while (c != NULL && c->window != window) c=c->next;
  printf("Found client %p\n", c);
  return c;
}

static void print_desktop(char * message) {
  Client *c = desktop.head;
  int i = 0;
  printf("%s\n", message);
  while (c != NULL) {
    printf("%d - %p\n", ++i, c);
    c=c->next;
  }
  fflush(stdout);
}

static void remove_client(Client *client) {
  printf("Removing client %p\n", client);
  fflush(stdout);
  if (client == NULL) {
    printf("Client is NULL.\n");
    fflush(stdout);
    return;
  }
  print_desktop("before remove_client");
  Client *curr = desktop.head;
  Client *prev = NULL;
  // TODO: search on all desktops
  while (curr != NULL && curr != client) {
    prev = curr;
    curr = curr->next;
  }
  if (curr != client) {
    printf("Client not found\n");
    fflush(stdout);
    return;
  } else {
    printf("Found client %p\n", curr);
    fflush(stdout);
  }
  if (prev && curr->next) {
    prev->next = curr->next;
    printf("Replacing previous\n");
    fflush(stdout);
  } else if (prev) {
    prev->next = NULL;
  }
  if (curr == desktop.head) {
    desktop.head = NULL;
    printf("Removing head\n");
    fflush(stdout);
  }

  print_desktop("after remove_client");
  fflush(stdout);
  free(curr);
}

static void destroy_notify(xcb_window_t window) {
  printf("Destroying client\n");
  fflush(stdout);
  Client *client = get_client(window);
  remove_client(client);
}

static void event_loop() {
  xcb_generic_event_t *event;
  // TODO: switch to non-blocking xcb_poll_for_event
  while ((event = xcb_wait_for_event(connection))) {
    printf("Received event %d, %d\n", event->response_type & ~0x80, event->response_type);
    switch (event->response_type & ~0x80) {
      case XCB_MAP_REQUEST:
        {
          printf("MAP_REQUEST\n");
          xcb_window_t window = ((xcb_map_request_event_t*)event)->window;
          xcb_map_window(connection, window);
          fullscreen(window);
          // TODO: malloc?
          Client *c = malloc(sizeof(Client));
          (*c).window = window;
          (*c).next = NULL;
          Client *p = desktop.head;
          if (desktop.head == NULL) {
            desktop.head = c;
          } else {
            while (p->next != NULL) {
              p = p->next;
            }
            p->next = c;
          }
          printf("Head: %p, Next: %p\n", desktop.head, desktop.head->next);
          if (desktop.head->next != NULL)
            printf("Next: %p, Next Next: %p\n", desktop.head->next, desktop.head->next->next);
          tile();
          break;
        }

      case XCB_CONFIGURE_NOTIFY: printf("XCB_CONFIGURE_NOTIFY\n"); break;
      case XCB_EXPOSE: printf("EXPOSE\n"); break;
      case XCB_BUTTON_PRESS: printf("BUTTON_PRESS\n"); break;
                             // SubstructureNotify
      case XCB_DESTROY_NOTIFY: 
                             {
                               printf("XCB_DESTROY_NOTIFY\n");
                               xcb_window_t window = ((xcb_destroy_notify_event_t*)event)->window;
                               destroy_notify(window);
                               tile();
                               break;
                             }
      case XCB_UNMAP_NOTIFY: printf("XCB_UNMAP_NOTIFY\n"); break;
      case XCB_MAP_NOTIFY: printf("XCB_MAP_NOTIFY\n"); break;
      default:
                           break;
    }
    fflush(stdout);
    xcb_flush(connection);
    free(event);
  }
}

int main() {
  printf("Initializing rwm\n");
  connect();
  setup();
  event_loop();
  disconnect();
  return 0;
}
