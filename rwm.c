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

static void event_loop() {
  xcb_generic_event_t *event;
  // TODO: switch to non-blocking xcb_poll_for_event
  while ((event = xcb_wait_for_event(connection))) {
    printf("Received event %d\n", event->response_type & ~0x80);
    switch (event->response_type & ~0x80) {
      case XCB_MAP_REQUEST:
        {
          printf("MAP_REQUEST\n");
          xcb_window_t window = ((xcb_map_request_event_t*)event)->window;
          xcb_map_window(connection, window);
          fullscreen(window);
          break;
        }
      case XCB_EXPOSE:
        {
          printf("EXPOSE\n");
          break;
        }
      case XCB_BUTTON_PRESS:
        {
          printf("BUTTON_PRESS\n");
          break;
        }
      default:
        break;
    }
    fflush(stdout);
    xcb_flush(connection);
    free(event);
  }
  printf(".");
  fflush(stdout);
  sleep(1);
}

int main() {
  printf("Initializing rwm\n");
  connect();
  setup();
  event_loop();
  disconnect();
  return 0;
}
