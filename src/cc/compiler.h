/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef SRC_CC_COMPILER_H
#define SRC_CC_COMPILER_H

#include <inc/cc/compiler.h>

#include <inc/types.h>

enum cc_token_type {
#define DEF(t)	CC_TOKEN_ ## t,
#include <inc/cpp/tokens.h>
#include <inc/cc/tokens.h>
#undef DEF
};

struct cc_token {
	enum cc_token_type	type;

	/* For identifiers, this is the cpp's resolved name. i.e. any esc-seqs in
	 * the original src was resolved to the corresponding src-char-set-encoded
	 * byte stream (src-char-set is assume to be utf-8.)
	 * That stream is stored here.
	 *
	 * For string-literals and char-consts, this is their exec-char-set
	 * representation.
	 */
	const char	*string;
	size_t		string_len;	/* doesn't include the nul char */
};

static inline
const char *cc_token_string(const struct cc_token *this)
{
	return this->string;
}

static inline
size_t cc_token_string_length(const struct cc_token *this)
{
	return this->string_len;
}

static inline
enum cc_token_type cc_token_type(const struct cc_token *this)
{
	return this->type;
}

static inline
bool cc_token_is_predefined_const(const struct cc_token *this)
{
	return (cc_token_type(this) == CC_TOKEN_TRUE ||
			cc_token_type(this) == CC_TOKEN_FALSE ||
			cc_token_type(this) == CC_TOKEN_NULL_PTR);
}

/* a keyword is also an identifier */
static inline
bool cc_token_is_keyword(const struct cc_token *this)
{
	return (cc_token_type(this) >= CC_TOKEN_ATOMIC &&
			cc_token_type(this) <= CC_TOKEN_WHILE);
}

static inline
bool cc_token_is_identifier(const struct cc_token *this)
{
	return (cc_token_type(this) >= CC_TOKEN_IDENTIFIER &&
			cc_token_type(this) <= CC_TOKEN_WHILE);
}

static inline
bool cc_token_is_string_literal(const struct cc_token *this)
{
	return (cc_token_type(this) >= CC_TOKEN_CHAR_STRING_LITERAL &&
			cc_token_type(this) <= CC_TOKEN_WCHAR_T_STRING_LITERAL);
}

static inline
bool cc_token_is_char_const(const struct cc_token *this)
{
	return (cc_token_type(this) >= CC_TOKEN_INTEGER_CHAR_CONST &&
			cc_token_type(this) <= CC_TOKEN_WCHAR_T_CHAR_CONST);
}

void cc_token_delete(void *this);
/*****************************************************************************/
struct cc_token_stream {
	const char	*buffer;	/* file containing the cpp_tokens */
	size_t		buffer_size;
	struct queue	tokens;		/* queue of cc_tokens */
};

static inline
void cc_token_stream_init(struct cc_token_stream *this,
						  const char *buffer,
						  const size_t buffer_size)
{
	this->buffer = buffer;
	this->buffer_size = buffer_size;
	queue_init(&this->tokens, cc_token_delete);
}

static inline
bool cc_token_stream_is_empty(const struct cc_token_stream *this)
{
	return queue_is_empty(&this->tokens);
}

static inline
err_t cc_token_stream_add_tail(struct cc_token_stream *this,
							   struct cc_token *token)
{
	return queue_add_tail(&this->tokens, token);
}

static inline
err_t cc_token_stream_add_head(struct cc_token_stream *this,
							   struct cc_token *token)
{
	return queue_add_head(&this->tokens, token);
}

static inline
void cc_token_stream_empty(struct cc_token_stream *this)
{
	queue_empty(&this->tokens);
}
/*****************************************************************************/
struct parse_node {
	enum cc_token_type	type;
};
/*****************************************************************************/
struct compiler {
	int	cpp_tokens_fd;
	const char	*cpp_tokens_path;
	struct cc_token_stream	stream;
};
#endif
