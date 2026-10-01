#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../util/networking.h"

#define CHAT_PORT 8080
#define MAX_LINE_LENGTH 1024

static int sendAll(int fd, const char *data, size_t length) {
  size_t sent = 0;
  while (sent < length) {
    ssize_t result = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
    if (result > 0) {
      sent += (size_t)result;
    } else if (result == -1 && errno == EINTR) {
      continue;
    } else {
      return -1;
    }
  }
  return 0;
}

static void *receiveMessages(void *argument) {
  int fd = *(int *)argument;
  char buffer[MAX_LINE_LENGTH];

  for (;;) {
    ssize_t bytesRead = recv(fd, buffer, sizeof buffer - 1, 0);
    if (bytesRead > 0) {
      buffer[bytesRead] = '\0';
      printf("\r%s> ", buffer);
      fflush(stdout);
    } else if (bytesRead == 0) {
      printf("\nServer closed the connection.\n");
      break;
    } else if (errno != EINTR) {
      perror("recv");
      break;
    }
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  signal(SIGPIPE, SIG_IGN);

  const char *serverIP = argc > 1 ? argv[1] : "127.0.0.1";
  const char *name = argc > 2 ? argv[2] : NULL;

  int socketFD = createTCPIPv4Socket();
  if (socketFD == -1) {
    return EXIT_FAILURE;
  }

  struct sockaddr_in address;
  if (createIPv4Address(serverIP, CHAT_PORT, &address) == -1) {
    fprintf(stderr, "Invalid server IPv4 address: %s\n", serverIP);
    close(socketFD);
    return EXIT_FAILURE;
  }

  if (connect(socketFD, (struct sockaddr *)&address, sizeof address) == -1) {
    perror("connect");
    close(socketFD);
    return EXIT_FAILURE;
  }

  printf("\n╔══════════════════════════════╗\n"
         "║       Welcome to SocketSpark ║\n"
         "╚══════════════════════════════╝\n"
         "Connected to %s:%d\n"
         "Commands: /name NAME, /help, /quit\n\n",
         serverIP, CHAT_PORT);

  pthread_t receiver;
  int threadResult = pthread_create(&receiver, NULL, receiveMessages, &socketFD);
  if (threadResult != 0) {
    errno = threadResult;
    perror("pthread_create");
    close(socketFD);
    return EXIT_FAILURE;
  }

  if (name != NULL) {
    char command[MAX_LINE_LENGTH];
    int written = snprintf(command, sizeof command, "/name %s\n", name);
    if (written < 0 || (size_t)written >= sizeof command ||
        sendAll(socketFD, command, (size_t)written) == -1) {
      perror("send");
    }
  }

  char input[MAX_LINE_LENGTH];
  while (true) {
    printf("> ");
    fflush(stdout);
    if (fgets(input, sizeof input, stdin) == NULL) {
      sendAll(socketFD, "/quit\n", 6);
      break;
    }

    if (strchr(input, '\n') == NULL) {
      printf("Message too long (maximum %d characters).\n", MAX_LINE_LENGTH - 2);
      int character;
      while ((character = getchar()) != '\n' && character != EOF) {
      }
      continue;
    }

    if (sendAll(socketFD, input, strlen(input)) == -1) {
      perror("send");
      break;
    }
    if (strcmp(input, "/quit\n") == 0) {
      break;
    }
  }

  shutdown(socketFD, SHUT_WR);
  pthread_join(receiver, NULL);
  close(socketFD);
  return EXIT_SUCCESS;
}
