/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef SRC_CPP_LEXER_H
#define SRC_CPP_LEXER_H

#include <inc/cpp/lexer.h>

struct code_point {
	struct lexer_position	begin;
	char32_t	cp;
	char		cp_size;
};

struct lexer {
	struct lexer_position	position;
	struct lexer_position	begin;
	const char	*file_path;
	const char	*dir_path;
	const char	*buffer;
	size_t		buffer_size;
};

static inline
size_t lexer_buffer_size(const struct lexer *this)
{
	return this->buffer_size;
}

static inline
struct lexer_position lexer_position(const struct lexer *this)
{
	return this->position;
}

static inline
void lexer_set_position(struct lexer *this,
						const struct lexer_position pos)
{
	this->position = pos;
}
#endif
