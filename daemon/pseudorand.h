#ifndef LIGHTNING_DAEMON_PSEUDORAND_H
#define LIGHTNING_DAEMON_PSEUDORAND_H
#include "config.h"
#include <stdint.h>

/**
 * pseudorand - pseudo (guessable!) random number between 0 and max-1.
 */
uint64_t pseudorand(uint64_t max);
#endif /* LIGHTNING_DAEMON_PSEUDORAND_H */
