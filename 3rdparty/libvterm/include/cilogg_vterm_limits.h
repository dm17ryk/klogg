/* Copyright (C) 2026 CILogg contributors
 * SPDX-License-Identifier: MIT
 * CILogg-only bounds extension for the pinned libvterm source. Not upstream API.
 */
#ifndef CILOGG_VTERM_LIMITS_H
#define CILOGG_VTERM_LIMITS_H
#include "vterm.h"
#define CILOGG_VTERM_MAX_STRING_BYTES 65536u
#define CILOGG_VTERM_MAX_PARAMETER 65535L
#define CILOGG_VTERM_MAX_PARAMETER_DIGITS 16u
#define CILOGG_VTERM_MAX_WIRE_BYTES (CILOGG_VTERM_MAX_STRING_BYTES + 512u)
typedef enum {
  CILOGG_VTERM_LIMIT_NONE = 0,
  CILOGG_VTERM_LIMIT_STRING = 1,
  CILOGG_VTERM_LIMIT_PARAMETER = 2,
  CILOGG_VTERM_LIMIT_ARGUMENTS = 3,
  CILOGG_VTERM_LIMIT_WORK = 4,
  CILOGG_VTERM_LIMIT_INVALID = 5
} CILoggVTermLimit;
#ifdef __cplusplus
extern "C" {
#endif
CILoggVTermLimit cilogg_vterm_resource_limit(const VTerm *vt);
#ifdef __cplusplus
}
#endif
#endif
