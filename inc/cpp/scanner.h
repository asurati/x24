/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef INC_CPP_SCANNER_H
#define INC_CPP_SCANNER_H

#include <inc/errno.h>

struct scanner;
err_t	scanner_new(struct scanner **out);
err_t	scanner_delete(struct scanner *this);
err_t	scanner_scan(struct scanner *this,
					 const char *path);
const char	*scanner_cpp_tokens_path(const struct scanner *this);
#endif
