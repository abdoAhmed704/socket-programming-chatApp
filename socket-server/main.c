#include <ctype.h>
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
#define MAX_CLIENTS 64
#define MAX_NAME_LENGTH 31
#define MAX_LINE_LENGTH 1024
#define RECEIVE_BUFFER_SIZE 2048

typedef struct {
  int fd;
  char name[MAX_NAME_LENGTH + 1];
} Client;

static Client clients[MAX_CLIENTS];
static size_t clientCount = 0;
static pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;

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

static void sendToClient(int fd, const char *message) {
  pthread_mutex_lock(&clientsMutex);
  if (sendAll(fd, message, strlen(message)) == -1) {
    perror("send");
  }
  pthread_mutex_unlock(&clientsMutex);
}

static void broadcast(const char *message, int excludedFD) {
  pthread_mutex_lock(&clientsMutex);
  for (size_t i = 0; i < clientCount; ++i) {
    if (clients[i].fd != excludedFD &&
        sendAll(clients[i].fd, message, strlen(message)) == -1) {
      perror("send");
    }
  }
  pthread_mutex_unlock(&clientsMutex);
}

static bool isValidName(const char *name) {
  size_t length = strlen(name);
  if (length == 0 || length > MAX_NAME_LENGTH) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    if (!isalnum((unsigned char)name[i]) && name[i] != '_' && name[i] != '-') {
      return false;
    }
  }
  return true;
}

static void getClientName(int fd, char *name, size_t size) {
  pthread_mutex_lock(&clientsMutex);
  for (size_t i = 0; i < clientCount; ++i) {
    if (clients[i].fd == fd) {
      snprintf(name, size, "%s", clients[i].name);
      break;
    }
  }
  pthread_mutex_unlock(&clientsMutex);
}

static bool handleLine(int fd, char *line) {
  line[strcspn(line, "\r\n")] = '\0';

  if (strcmp(line, "/quit") == 0) {
    sendToClient(fd, "Goodbye! Thanks for chatting.\n");
    return false;
  }

  if (strncmp(line, "/name ", 6) == 0) {
    const char *newName = line + 6;
    if (!isValidName(newName)) {
      sendToClient(fd, "Name must be 1-31 letters, numbers, _ or -.\n");
      return true;
    }

    char oldName[MAX_NAME_LENGTH + 1] = "guest";
    pthread_mutex_lock(&clientsMutex);
    for (size_t i = 0; i < clientCount; ++i) {
      if (clients[i].fd == fd) {
        snprintf(oldName, sizeof oldName, "%s", clients[i].name);
        snprintf(clients[i].name, sizeof clients[i].name, "%s", newName);
        break;
      }
    }
    pthread_mutex_unlock(&clientsMutex);

    char notice[MAX_LINE_LENGTH];
    snprintf(notice, sizeof notice, "* %s is now known as %s\n", oldName, newName);
    broadcast(notice, -1);
    return true;
  }

  if (strcmp(line, "/help") == 0) {
    sendToClient(fd, "Commands: /name NAME, /help, /quit\n");
    return true;
  }

  if (line[0] == '/') {
    sendToClient(fd, "Unknown command. Type /help for available commands.\n");
    return true;
  }

  if (line[0] == '\0') {
    return true;
  }

  char name[MAX_NAME_LENGTH + 1] = "guest";
  getClientName(fd, name, sizeof name);

  char message[MAX_LINE_LENGTH + MAX_NAME_LENGTH + 4];
  snprintf(message, sizeof message, "[%s] %s\n", name, line);
  broadcast(message, -1);
  return true;
}

static void removeClient(int fd, char *name, size_t nameSize) {
  pthread_mutex_lock(&clientsMutex);
  for (size_t i = 0; i < clientCount; ++i) {
    if (clients[i].fd == fd) {
      snprintf(name, nameSize, "%s", clients[i].name);
      clients[i] = clients[clientCount - 1];
      --clientCount;
      break;
    }
  }
  pthread_mutex_unlock(&clientsMutex);
}

static void *clientWorker(void *argument) {
  int fd = *(int *)argument;
  free(argument);

  char input[RECEIVE_BUFFER_SIZE];
  char line[MAX_LINE_LENGTH];
  size_t lineLength = 0;
  bool connected = true;

  sendToClient(fd, "Welcome to SocketSpark! Type /help for commands.\n");

  while (connected) {
    ssize_t received = recv(fd, input, sizeof input, 0);
    if (received == 0) {
      break;
    }
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("recv");
      break;
    }

    for (ssize_t i = 0; i < received; ++i) {
      if (input[i] == '\n') {
        line[lineLength] = '\0';
        connected = handleLine(fd, line);
        lineLength = 0;
        if (!connected) {
          break;
        }
      } else if (lineLength < sizeof line - 1) {
        line[lineLength++] = input[i];
      } else {
        lineLength = 0;
        sendToClient(fd, "Message too long (maximum 1023 characters).\n");
      }
    }
  }

  char name[MAX_NAME_LENGTH + 1] = "guest";
  removeClient(fd, name, sizeof name);
  close(fd);

  char notice[MAX_LINE_LENGTH];
  snprintf(notice, sizeof notice, "* %s left the chat\n", name);
  broadcast(notice, -1);
  printf("[-] %s disconnected\n", name);
  return NULL;
}

int main(void) {
  signal(SIGPIPE, SIG_IGN);

  int serverFD = createTCPIPv4Socket();
  if (serverFD == -1) {
    return EXIT_FAILURE;
  }

  int enabled = 1;
  if (setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) == -1) {
    perror("setsockopt");
    close(serverFD);
    return EXIT_FAILURE;
  }

  struct sockaddr_in address;
  if (createIPv4Address("", CHAT_PORT, &address) == -1) {
    perror("createIPv4Address");
    close(serverFD);
    return EXIT_FAILURE;
  }

  if (bind(serverFD, (struct sockaddr *)&address, sizeof address) == -1) {
    perror("bind");
    close(serverFD);
    return EXIT_FAILURE;
  }
  if (listen(serverFD, MAX_CLIENTS) == -1) {
    perror("listen");
    close(serverFD);
    return EXIT_FAILURE;
  }

  printf("\n=== SocketSpark multi-user chat ===\nListening on 0.0.0.0:%d\n", CHAT_PORT);

  for (;;) {
    struct sockaddr_in clientAddress;
    socklen_t clientAddressSize = sizeof clientAddress;
    int clientFD = accept(serverFD, (struct sockaddr *)&clientAddress, &clientAddressSize);
    if (clientFD == -1) {
      if (errno == EINTR) {
        continue;
      }
      perror("accept");
      continue;
    }

    pthread_mutex_lock(&clientsMutex);
    if (clientCount == MAX_CLIENTS) {
      pthread_mutex_unlock(&clientsMutex);
      const char fullMessage[] = "Server is full. Please try again later.\n";
      sendAll(clientFD, fullMessage, sizeof fullMessage - 1);
      close(clientFD);
      continue;
    }
    clients[clientCount].fd = clientFD;
    snprintf(clients[clientCount].name, sizeof clients[clientCount].name,
             "guest%zu", clientCount + 1);
    char name[MAX_NAME_LENGTH + 1];
    snprintf(name, sizeof name, "%s", clients[clientCount].name);
    ++clientCount;
    pthread_mutex_unlock(&clientsMutex);

    int *workerFD = malloc(sizeof *workerFD);
    if (workerFD == NULL) {
      perror("malloc");
      removeClient(clientFD, name, sizeof name);
      close(clientFD);
      continue;
    }
    *workerFD = clientFD;

    pthread_t thread;
    int threadResult = pthread_create(&thread, NULL, clientWorker, workerFD);
    if (threadResult != 0) {
      errno = threadResult;
      perror("pthread_create");
      free(workerFD);
      removeClient(clientFD, name, sizeof name);
      close(clientFD);
      continue;
    }
    pthread_detach(thread);

    char notice[MAX_LINE_LENGTH];
    snprintf(notice, sizeof notice, "* %s joined the chat\n", name);
    broadcast(notice, clientFD);
    printf("[+] %s connected\n", name);
  }
}
