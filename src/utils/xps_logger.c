#include "../xps.h"

/**
 * @brief Logs messages with different log levels.
 *
 * This function logs messages with different log levels based on the specified level parameter.
 * It supports logging of error, info, debug, and warning messages. The log messages are printed
 * with appropriate colors for better readability.
 * If XPS_DEBUG env var is set to 1 then debug logs will be printed
 *
 * @param level The log level of the message.
 * @param function_name The name of the function where the log message originated.
 * @param format_string The format string for the log message.
 * @param ... Additional arguments for the format string.
 * @return void
 */
void logger(xps_log_level_t level, const char *function_name, const char *format_string, ...) {
  char *XPS_DEBUG = getenv("XPS_DEBUG");

  if ((XPS_DEBUG == NULL || strcmp(XPS_DEBUG, "1") != 0) && level == LOG_DEBUG)
    return;

  const char *log_level_strings[] = {"ERROR", "INFO", "DEBUG", "WARNING", "HTTP"};
  const char *log_level_colors[] = {RED_BG, BLUE_BG, MAGENTA_TEXT, YELLOW_BG, GREEN_BG};

  va_list args;
  va_start(args, format_string);

  printf("%s" BOLD_START " %s " BOLD_END RESET_COLOR " " GREEN_TEXT "%s" RESET_COLOR " : ",
         log_level_colors[level], log_level_strings[level], function_name);
  vprintf(format_string, args);
  printf("\n");

  fflush(stdout);

  va_end(args);
}