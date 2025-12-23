#include "../xps.h"

loop_event_t *loop_event_create(u_int fd, void *ptr, xps_handler_t read_cb);
void loop_event_destroy(loop_event_t *event);
int loop_search_event(vec_void_t *events, loop_event_t *event);

xps_loop_t *xps_loop_create(xps_core_t *core) {
  assert(core != NULL);

  // Creating epoll fd
  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    logger(LOG_ERROR, "xps_loop_create()", "epoll_create1() failed");
    perror("Error message");
    return NULL;
  }

  // Alloc memory for loop instance
  xps_loop_t *loop = malloc(sizeof(xps_loop_t));
  if (loop == NULL) {
    logger(LOG_ERROR, "xps_loop_create()", "malloc() failed for 'loop'");
    close(epoll_fd);
    return NULL;
  }

  // Init values
  loop->core = core;
  loop->epoll_fd = epoll_fd;
  loop->n_null_events = 0;
  vec_init(&(loop->events));

  logger(LOG_DEBUG, "xps_loop_create()", "created loop");

  return loop;
}

void xps_loop_destroy(xps_loop_t *loop) {
  assert(loop != NULL);

  // Destroy all events
  for (int i = 0; i < loop->events.length; i++) {
    loop_event_t *event = loop->events.data[i];
    if (event != NULL)
      loop_event_destroy(event);
  }
  vec_deinit(&(loop->events));

  close(loop->epoll_fd);
  free(loop);

  logger(LOG_DEBUG, "xps_loop_destroy()", "destroyed loop");
}

loop_event_t *loop_event_create(u_int fd, void *ptr, xps_handler_t read_cb) {
  assert(ptr != NULL);

  // Alloc memory for 'event' instance
  loop_event_t *event = malloc(sizeof(loop_event_t));
  if (event == NULL) {
    logger(LOG_ERROR, "loop_event_create()", "malloc() failed for 'event'");
    return NULL;
  }

  event->fd = fd;
  event->ptr = ptr;
  event->read_cb = read_cb;

  logger(LOG_DEBUG, "loop_event_create()", "created event");

  return event;
}

void loop_event_destroy(loop_event_t *event) {
  assert(event != NULL);
  free(event);
  logger(LOG_DEBUG, "loop_event_destroy()", "destroyed event");
}

int xps_loop_attach(xps_loop_t *loop, u_int fd, int event_flags, void *ptr, xps_handler_t read_cb) {
  assert(loop != NULL);
  assert(ptr != NULL);

  // Create event instance
  loop_event_t *event = loop_event_create(fd, ptr, read_cb);
  if (event == NULL) {
    logger(LOG_ERROR, "xps_loop_attach()", "loop_event_create() failed");
    return E_FAIL;
  }

  // Add fd to epoll
  struct epoll_event e;
  e.events = event_flags;
  e.data.fd = fd;
  e.data.ptr = (void *)event;

  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &e) < 0) {
    logger(LOG_ERROR, "xps_loop_attach()", "epoll_ctl() add failed");
    perror("Error message");
    loop_event_destroy(event);
    return E_FAIL;
  }

  // Add event to 'events' list of loop
  vec_push(&(loop->events), (void *)event);

  logger(LOG_DEBUG, "xps_loop_attach()", "attached to loop");

  return OK;
}

int xps_loop_detach(xps_loop_t *loop, u_int fd) {
  assert(loop != NULL);

  // Search for 'event' with given fd
  int event_index = -1;
  for (int i = 0; i < loop->events.length; i++) {
    loop_event_t *event = loop->events.data[i];
    if (event != NULL && event->fd == fd) {
      event_index = i;
      break;
    }
  }

  // Event with given fd not found
  if (event_index == -1) {
    logger(LOG_ERROR, "xps_loop_detach()", "failed to detach from loop. event not found");
    return E_FAIL;
  }

  // Removing fd from epoll
  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
    logger(LOG_ERROR, "xps_loop_detach()", "epoll_ctl() del failed");
    return E_FAIL;
  }

  // Destroying event
  loop_event_destroy((loop_event_t *)(loop->events.data[event_index]));

  // Setting NULL in events list
  loop->events.data[event_index] = NULL;
  loop->n_null_events += 1;

  logger(LOG_DEBUG, "xps_loop_detach()", "detached from loop");

  return OK;
}

void xps_loop_run(xps_loop_t *loop) {
  assert(loop != NULL);

  while (1) {
    logger(LOG_DEBUG, "xps_loop_run()", "epoll wait");
    int n_events = epoll_wait(loop->epoll_fd, loop->epoll_events, MAX_EPOLL_EVENTS, -1);
    logger(LOG_DEBUG, "xps_loop_run()", "epoll wait over");

    logger(LOG_DEBUG, "xps_loop_run()", "handling %d events", n_events);

    // Handle events
    for (int i = 0; i < n_events; i++) {
      logger(LOG_DEBUG, "xps_loop_run()", "handling event no. %d", i + 1);

      struct epoll_event curr_epoll_event = loop->epoll_events[i];
      loop_event_t *curr_event = curr_epoll_event.data.ptr;

      // Check if event still exists. Could have been destroyed due to prev event
      int curr_event_idx = loop_search_event(&(loop->events), curr_event);
      if (curr_event_idx == -1) {
        logger(LOG_DEBUG, "handle_epoll_events()", "event not found. skipping");
        continue;
      }

      // Read event
      if (curr_epoll_event.events & EPOLLIN) {
        logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / read");
        if (curr_event->read_cb != NULL)
          curr_event->read_cb(curr_event->ptr);
      }
    }
  }
}

int loop_search_event(vec_void_t *events, loop_event_t *event) {
  for (int i = 0; i < (*events).length; i++) {
    loop_event_t *curr = (*events).data[i];
    if (curr == event)
      return i;
  }
  return -1;
}