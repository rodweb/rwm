#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> /* pause() */
#include <sys/un.h>
#include <sys/socket.h>

int main() {
  int sock = 0;
  if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  char socket_path[256] = "/tmp/rwm-socket";
  struct sockaddr_un socket_address;
  socket_address.sun_family = AF_UNIX;
  strncpy(socket_address.sun_path, socket_path, sizeof(socket_address.sun_path) -1);

  if (connect(sock, (struct sockaddr*)&socket_address, sizeof(socket_address)) < 0) {
    perror("connection failed");
    exit(EXIT_FAILURE);
  }
  printf("connected\n");

  char *message = "Ping from client";
  char buffer[1024] = {0};
  send(sock, message, strlen(message), 0);
  printf("Message sent\n");
  read(sock, buffer, 1024);
  printf("%s\n", buffer);
  return 0;
}

