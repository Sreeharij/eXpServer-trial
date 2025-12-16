#include "../xps.h"

// Reverses string excluding the last character; '\n' in case of netcat client
void strrev(char *str) {
  for (int start = 0, end = strlen(str) - 2; start < end; start++, end--) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
  }
}

xps_connection_t *xps_connection_create(int epoll_fd, int conn_sock_fd) {
  xps_connection_t *connection = malloc(sizeof(xps_connection_t));
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
    return NULL;
  }

  // Attach socket fd to event loop
  xps_loop_attach(epoll_fd, conn_sock_fd, EPOLLIN);

  // Init values
  connection->epoll_fd = epoll_fd;
  connection->sock_fd = conn_sock_fd;
  connection->listener = NULL;
  connection->remote_ip = get_remote_ip(conn_sock_fd);

  // Add connection to 'connections' list
  vec_push(&connections, connection);

  logger(LOG_DEBUG, "xps_connection_create()", "created connection");
  return connection;
}

void xps_connection_destroy(xps_connection_t *connection) {
  assert(connection != NULL);

  // Set to NULL in 'connections' list
  for (int i = 0; i < connections.length; i++) {
    xps_connection_t *curr = connections.data[i];
    if (curr == connection) {
      connections.data[i] = NULL;
      break;
    }
  }

  xps_loop_detach(connection->epoll_fd, connection->sock_fd);
  close(connection->sock_fd);
  free(connection->remote_ip);
  free(connection);
  logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

void xps_connection_read_handler(xps_connection_t *connection) {
  assert(connection != NULL);

  char buff[DEFAULT_BUFFER_SIZE];

  long read_n = recv(connection->sock_fd, buff, sizeof(buff), 0);
  if (read_n < 0) {
    logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
    perror("Error message");
    xps_connection_destroy(connection);
    return;
  }
  if (read_n == 0) {
    logger(LOG_INFO, "connection_read_handler()", "peer closed connection");
    xps_connection_destroy(connection);
    return;
  }

  buff[read_n] = '\0';

  // Printing client message
  printf("[CLIENT MESSAGE] %s", buff);

  // Reverse client message
  strrev(buff);

  // Sending reversed message to client
  long bytes_written = 0;
  long message_len = read_n;
  while (bytes_written < message_len) {
    long write_n = send(connection->sock_fd, buff + bytes_written, message_len - bytes_written, 0);
    if (write_n < 0) {
      logger(LOG_ERROR, "xps_connection_read_handler()", "send() failed");
      perror("Error message");
      xps_connection_destroy(connection);
      return;
    }
    bytes_written += write_n;
  }
}