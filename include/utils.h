#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define log(msg, ...)                    \
  fprintf(stdout, (msg), ## __VA_ARGS__)

#define TRUE  1
#define FALSE 0

typedef int PoolOffset;
typedef int bool;

#endif
