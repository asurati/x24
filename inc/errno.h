/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef INC_ERRNO_H
#define INC_ERRNO_H

#include <errno.h>

typedef int	err_t;

#define ESUCCESS	0	/* Same as EXIT_SUCCESS in stdlib.h */
#define EBASE		32768
#define EPARTIAL	(EBASE + 1)
#endif
