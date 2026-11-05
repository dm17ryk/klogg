/* Copyright (C) 2026 CILogg contributors
 * SPDX-License-Identifier: MIT
 */
#include "vterm_internal.h"
CILoggVTermLimit cilogg_vterm_resource_limit(const VTerm *vt)
{
  return vt ? vt->resource_limit : CILOGG_VTERM_LIMIT_INVALID;
}
