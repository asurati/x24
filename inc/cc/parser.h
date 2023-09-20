/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef INC_CC_PARSER_H
#define INC_CC_PARSER_H

#include <inc/errno.h>

struct parser;
err_t	parser_new(const char *path,
				   struct parser **out);
err_t	parser_delete(struct parser *this);
err_t	parser_parse(struct parser *this);
#endif
