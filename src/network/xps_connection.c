#include "../xps.h"

void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);
void connection_source_handler(void *ptr);
void connection_source_close_handler(void *ptr);
void connection_sink_handler(void *ptr);
void connection_sink_close_handler(void *ptr);
void connection_close(xps_connection_t *connection, bool peer_closed);

xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd) {
  assert(core != NULL);

  // Alloc memory for connection instance
  xps_connection_t *connection = malloc(sizeof(xps_connection_t));
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_create()",
           "malloc() failed for 'connection'");
    return NULL;
  }

  // Create source instance
  xps_pipe_source_t *source =
      xps_pipe_source_create((void *)connection, connection_source_handler,
                             connection_source_close_handler);
  if (source == NULL) {
    logger(LOG_ERROR, "xps_connection_create()",
           "xps_pipe_source_create() failed");
    free(connection);
    return NULL;
  }

  // Create sink instance
  xps_pipe_sink_t *sink =
      xps_pipe_sink_create((void *)connection, connection_sink_handler,
                           connection_sink_close_handler);
  if (sink == NULL) {
    logger(LOG_ERROR, "xps_connection_create()",
           "xps_pipe_sink_create() failed");
    xps_pipe_source_destroy(source);
    free(connection);
    return NULL;
  }

  // Init values
  connection->core = core;
  connection->sock_fd = sock_fd;
  connection->source = source;
  connection->sink = sink;
  connection->listener = NULL;
  connection->remote_ip = get_remote_ip(sock_fd);

  // Attach connection to loop
  if (xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET,
                      (void *)connection, connection_loop_read_handler,
                      connection_loop_write_handler,
                      connection_loop_close_handler) != OK) {
    logger(LOG_ERROR, "xps_connection_create()", "xps_loop_attach() failed");
    xps_pipe_source_destroy(source);
    xps_pipe_sink_destroy(sink);
    free(connection);
    return NULL;
  }

  logger(LOG_DEBUG, "xps_connection_create()", "created connection");

  return connection;
}

void xps_connection_destroy(xps_connection_t *connection) {
  assert(connection != NULL);

  // Detach connection from loop
  if (xps_loop_detach(connection->core->loop, connection->sock_fd) != OK)
    logger(LOG_ERROR, "xps_connection_destroy()", "xps_loop_detach() failed");

  xps_pipe_source_destroy(connection->source);
  xps_pipe_sink_destroy(connection->sink);
  close(connection->sock_fd);
  free(connection->remote_ip);

  free(connection);
  logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

void connection_loop_read_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;
  connection->source->ready = true;
}

void connection_loop_write_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;
  connection->sink->ready = true;
}

void connection_loop_close_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;
  connection_close(connection, true);
}

void connection_source_handler(void *ptr) {
  assert(ptr != NULL);
  xps_pipe_source_t *source = ptr;
  xps_connection_t *connection = source->ptr;

  xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);
  if (buff == NULL) {
    logger(LOG_DEBUG, "connection_source_handler()",
           "xps_buffer_create() failed");
    return;
  }

  // Read from socket
  long read_n = recv(connection->sock_fd, buff->data, buff->size, 0);
  buff->len = read_n;

  // Socket would block
  if (read_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    xps_buffer_destroy(buff);
    connection->source->ready = false;
    return;
  }

  // Socket error
  if (read_n < 0) {
    xps_buffer_destroy(buff);
    logger(LOG_ERROR, "connection_source_handler()", "recv() failed");
    connection_close(connection, false);
    return;
  }

  // Peer closed connection
  if (read_n == 0) {
    xps_buffer_destroy(buff);
    connection_close(connection, true);
    return;
  }

  if (xps_pipe_source_write(source, buff) != OK) {
    logger(LOG_ERROR, "connection_source_handler()",
           "xps_pipe_source_write() failed");
    xps_buffer_destroy(buff);
    connection_close(connection, false);
    return;
  }

  xps_buffer_destroy(buff);
}

void connection_source_close_handler(void *ptr) {
  assert(ptr != NULL);
  xps_pipe_source_t *source = ptr;
  xps_connection_t *connection = source->ptr;

  if (!(connection->source->active) && !(connection->sink->active))
    connection_close(connection, false);
}

void connection_sink_handler(void *ptr) {
  assert(ptr != NULL);
  xps_pipe_sink_t *sink = ptr;
  xps_connection_t *connection = sink->ptr;

  xps_buffer_t *buff = xps_pipe_sink_read(sink, sink->pipe->buff_list->len);
  if (buff == NULL) {
    logger(LOG_ERROR, "connection_sink_handler()",
           "xps_pipe_sink_read() failed");
    return;
  }

  // Write to socket
  int write_n = send(connection->sock_fd, buff->data, buff->len, MSG_NOSIGNAL);

  xps_buffer_destroy(buff);

  // Socket would block
  if (write_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    sink->ready = false;
    return;
  }

  // Socket error
  if (write_n < 0) {
    logger(LOG_ERROR, "connection_sink_handler()", "send() failed");
    connection_close(connection, false);
    return;
  }

  if (write_n == 0)
    return;

  // Clear write_n length from pipe buff_list
  if (xps_pipe_sink_clear(sink, write_n) != OK)
    logger(LOG_ERROR, "connection_sink_handler()",
           "failed to clear %d bytes from sink", write_n);
}

void connection_sink_close_handler(void *ptr) {
  assert(ptr != NULL);
  xps_pipe_sink_t *sink = ptr;
  xps_connection_t *connection = sink->ptr;

  if (!(connection->source->active) && !(connection->sink->active))
    connection_close(connection, false);
}

void connection_close(xps_connection_t *connection, bool peer_closed) {
  assert(connection != NULL);
  logger(LOG_INFO, "connection_close()",
         peer_closed ? "peer closed connection" : "closing connection");
  xps_connection_destroy(connection);
}