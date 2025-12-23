#include "../xps.h"

void connection_loop_read_handler(void *ptr);

// Reverses string excluding the last character; '\n' in case of netcat client
void strrev(char *str) {
  for (int start = 0, end = strlen(str) - 2; start < end; start++, end--) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
  }
}

xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd) {
  assert(core != NULL);

  xps_connection_t *connection = malloc(sizeof(xps_connection_t));
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
    return NULL;
  }

  // Init values
  connection->core = core;
  connection->sock_fd = sock_fd;
  connection->listener = NULL;
  connection->remote_ip = get_remote_ip(sock_fd);

  // Attach connection to event loop
  xps_loop_attach(core->loop, sock_fd, EPOLLIN, connection, connection_loop_read_handler);

  // Add connection to 'connections' list
  vec_push(&core->connections, connection);

  logger(LOG_DEBUG, "xps_connection_create()", "created connection");
  return connection;
}


void xps_connection_destroy(xps_connection_t *connection) {
  assert(connection != NULL);

  // Set to NULL in 'connections' list
  for (int i = 0; i < connection->core->connections.length; i++) {
    xps_connection_t *curr = connection->core->connections.data[i];
    if (curr == connection) {
      connection->core->connections.data[i] = NULL;
      break;
    }
  }

  xps_loop_detach(connection->core->loop, connection->sock_fd);
  close(connection->sock_fd);
  free(connection->remote_ip);
  free(connection);
  logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

void connection_loop_read_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

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



