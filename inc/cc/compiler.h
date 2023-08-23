/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef INC_CC_COMPILER_H
#define INC_CC_COMPILER_H

#include <inc/errno.h>

struct compiler;
err_t	compiler_new(const char *path,
					 struct compiler **out);
err_t	compiler_delete(struct compiler *this);
err_t	compiler_compile(struct compiler *this);
#endif
