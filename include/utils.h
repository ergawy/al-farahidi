#ifndef UTILS_H
#define UTILS_H

#define log(msg, ...)                    \
  fprintf(stdout, (msg), ## __VA_ARGS__)

#define TRUE  1
#define FALSE 0

#endif
