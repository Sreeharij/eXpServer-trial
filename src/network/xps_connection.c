#include "../xps.h"
#include <asm-generic/errno.h>
#include <errno.h>
#include <sys/epoll.h>

void connection_read_handler(void *ptr);
void connection_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);
void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);

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
    logger(LOG_ERROR, "xps_connection_create()",
           "malloc() failed for 'connection'");
    return NULL;
  }

  xps_buffer_list_t *write_buff_list = xps_buffer_list_create();
  if (write_buff_list == NULL) {
    logger(LOG_ERROR, "xps_connection_create()",
           "xps_buffer_list_create() failed");
    free(connection);
    return NULL;
  }

  // Init values
  connection->core = core;
  connection->sock_fd = sock_fd;
  connection->listener = NULL;
  connection->remote_ip = get_remote_ip(sock_fd);
  connection->write_buff_list = write_buff_list;

  connection->read_ready = false;
  connection->write_ready = false;

  connection->send_handler = connection_write_handler;
  connection->recv_handler = connection_read_handler;

  // Attach connection to event loop
  xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET, connection,
                  connection_loop_read_handler, connection_loop_write_handler,
                  connection_loop_close_handler);

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
  xps_buffer_list_destroy(connection->write_buff_list);
  free(connection->remote_ip);
  free(connection);
  logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

void connection_read_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  char buff[DEFAULT_BUFFER_SIZE];

  long read_n = recv(connection->sock_fd, buff, DEFAULT_BUFFER_SIZE - 1, 0);
  if (read_n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      connection->read_ready = false;
      return;
    } else {
      logger(LOG_ERROR, "connection_read_handler()", "recv() failed");
      perror("Error message");
      xps_connection_destroy(connection);
      return;
    }
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

  // Add to write_buff_list
  xps_buffer_t *temp = xps_buffer_create(read_n, read_n, NULL);
  memcpy(temp->data, buff, read_n);
  xps_buffer_list_append(connection->write_buff_list, temp);
}

void connection_write_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  if (connection->write_buff_list->len == 0)
    return;

  xps_buffer_t *buff = xps_buffer_list_read(connection->write_buff_list,
                                            connection->write_buff_list->len);

  long write_n = send(connection->sock_fd, buff->data, buff->len, 0);

  if (write_n == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      connection->write_ready = false;
      xps_buffer_destroy(buff);
      return;
    } else {
      logger(LOG_ERROR, "connection_write_handler()", "send() failed");
      perror("Error message");
      xps_buffer_destroy(buff);
      xps_connection_destroy(connection);
      return;
    }
  }

  if (write_n > 0) {
    xps_buffer_list_clear(connection->write_buff_list, write_n);
  }

  xps_buffer_destroy(buff);
}

void connection_loop_close_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  logger(LOG_INFO, "connection_loop_close_handler()", "peer closed connection");
  xps_connection_destroy(connection);
}

void connection_loop_read_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  connection->read_ready = true;
}

void connection_loop_write_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  connection->write_ready = true;
}
