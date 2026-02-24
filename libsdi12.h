/**
 * @file libsdi12.h
 * @brief Convenience header — includes the entire libsdi12 API.
 *
 * This header exists so that `#include <libsdi12.h>` works out of the
 * box.  It simply pulls in the common types, sensor API, master API,
 * and the beginner-friendly easy macros.
 */
#ifndef LIBSDI12_H
#define LIBSDI12_H

#include "sdi12.h"
#include "sdi12_sensor.h"
#include "sdi12_master.h"
#include "sdi12_easy.h"

#endif /* LIBSDI12_H */
