#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> /* pause() */
#include <sys/un.h>
#include <sys/socket.h>
#include "common.h"

int main(int argc, char **argv) {
  int socket_fd = 0;
  if ((socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_un socket_address;
  memset(&socket_address, 0, sizeof(struct sockaddr_un));
  socket_address.sun_family = AF_UNIX;
  strncpy(socket_address.sun_path, RWM_SOCK_PATH, sizeof(socket_address.sun_path) - 1);

  if (connect(socket_fd, (struct sockaddr*)&socket_address, sizeof(struct sockaddr_un)) < 0) {
    perror("connection failed");
    exit(EXIT_FAILURE);
  }

  char message[BUFFER_SIZE] = "version";
  if (argc > 1) strncpy(message, argv[1], sizeof(message));
  send(socket_fd, message, sizeof(message), 0);

  char buffer[BUFFER_SIZE] = {0};
  read(socket_fd, buffer, BUFFER_SIZE);
  printf("%s\n", buffer);
  exit(EXIT_SUCCESS);
}

