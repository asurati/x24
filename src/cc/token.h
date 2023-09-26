/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef SRC_CC_TOKEN_H
#define SRC_CC_TOKEN_H

#include <inc/types.h>

/*
 * Only terminals, including char-consts, string-literals, integer-consts,
 * floating-consts, punctuators, keywords and identifiers.
 */
enum cc_token_type {
#define DEF(t)	CC_TOKEN_ ## t,
#define NODE(t)
#include <inc/cpp/tokens.h>	/* grammar terminals */
#include <inc/cc/tokens.h>	/* grammar non-terminals */
#undef DEF
#undef NODE
};

static inline
bool cc_token_type_is_terminal(const enum cc_token_type this)
{
	assert(this != CC_TOKEN_NUMBER && this != CC_TOKEN_INVALID);
	return this > CC_TOKEN_INVALID && this <= CC_TOKEN_WHILE;
}

static inline
bool cc_token_type_is_non_terminal(const enum cc_token_type this)
{
	return !cc_token_type_is_terminal(this);
}

static inline
bool cc_token_type_is_key_word(const enum cc_token_type this)
{
	return this >= CC_TOKEN_ATOMIC && this <= CC_TOKEN_WHILE;
}

static inline
bool cc_token_type_is_punctuator(const enum cc_token_type this)
{
	return this >= CC_TOKEN_LEFT_BRACE && this <= CC_TOKEN_ELLIPSIS;
}

static inline
bool cc_token_type_is_identifier(const enum cc_token_type this)
{
	return this == CC_TOKEN_IDENTIFIER;
}

static inline
bool cc_token_type_is_string_literal(const enum cc_token_type this)
{
	return (this >= CC_TOKEN_CHAR_STRING_LITERAL &&
			this <= CC_TOKEN_WCHAR_T_STRING_LITERAL);
}

static inline
bool cc_token_type_is_char_const(const enum cc_token_type this)
{
	return (this >= CC_TOKEN_INTEGER_CHAR_CONST &&
			this <= CC_TOKEN_WCHAR_T_CHAR_CONST);
}

static inline
bool cc_token_type_is_number(const enum cc_token_type this)
{
	return this >= CC_TOKEN_INTEGER_CONST && this <= CC_TOKEN_FLOATING_CONST;
}

static inline
bool cc_token_type_is_storage_specifier(const enum cc_token_type this)
{
	return (this == CC_TOKEN_AUTO ||
			this == CC_TOKEN_CONST_EXPR ||
			this == CC_TOKEN_EXTERN ||
			this == CC_TOKEN_REGISTER ||
			this == CC_TOKEN_STATIC ||
			this == CC_TOKEN_THREAD_LOCAL ||
			this == CC_TOKEN_TYPE_DEF);
}

static inline
bool cc_token_type_is_type_specifier(const enum cc_token_type this)
{
	return (this == CC_TOKEN_VOID ||
			this == CC_TOKEN_CHAR ||
			this == CC_TOKEN_SHORT ||
			this == CC_TOKEN_INT ||
			this == CC_TOKEN_LONG ||
			this == CC_TOKEN_FLOAT ||
			this == CC_TOKEN_DOUBLE ||
			this == CC_TOKEN_SIGNED ||
			this == CC_TOKEN_UNSIGNED ||
			this == CC_TOKEN_BIT_INT ||
			this == CC_TOKEN_BOOL ||
			this == CC_TOKEN_COMPLEX ||
			this == CC_TOKEN_DECIMAL_32 ||
			this == CC_TOKEN_DECIMAL_64 ||
			this == CC_TOKEN_DECIMAL_128 ||
			this == CC_TOKEN_ATOMIC ||
			this == CC_TOKEN_STRUCT ||
			this == CC_TOKEN_UNION ||
			this == CC_TOKEN_ENUM ||
			this == CC_TOKEN_TYPE_OF ||
			this == CC_TOKEN_TYPE_OF_UNQUAL ||
			this == CC_TOKEN_IDENTIFIER);
}

static inline
bool cc_token_type_is_type_qualifier(const enum cc_token_type this)
{
	return (this == CC_TOKEN_CONST ||
			this == CC_TOKEN_RESTRICT ||
			this == CC_TOKEN_VOLATILE ||
			this == CC_TOKEN_ATOMIC);
}

static inline
bool cc_token_type_is_alignment_specifier(const enum cc_token_type this)
{
	return this == CC_TOKEN_ALIGN_AS;
}

static inline
bool cc_token_type_is_function_specifier(const enum cc_token_type this)
{
	return this == CC_TOKEN_INLINE || this == CC_TOKEN_NO_RETURN;
}
/*****************************************************************************/
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
void cc_token_init(struct cc_token *this)
{
	this->type = CC_TOKEN_INVALID;
	this->string = NULL;
	this->string_len = 0;
}

static inline
void cc_token_reset_string(struct cc_token *this)
{
	this->string = NULL;
	this->string_len = 0;
}

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

/* a keyword is also an identifier */
static inline
bool cc_token_is_key_word(const struct cc_token *this)
{
	return cc_token_type_is_key_word(cc_token_type(this));
}

static inline
bool cc_token_is_punctuator(const struct cc_token *this)
{
	return cc_token_type_is_punctuator(cc_token_type(this));
}

static inline
bool cc_token_is_identifier(const struct cc_token *this)
{
	return cc_token_type_is_identifier(cc_token_type(this));
}

static inline
bool cc_token_is_string_literal(const struct cc_token *this)
{
	return cc_token_type_is_string_literal(cc_token_type(this));
}

static inline
bool cc_token_is_char_const(const struct cc_token *this)
{
	return cc_token_type_is_char_const(cc_token_type(this));
}

static inline
bool cc_token_is_number(const struct cc_token *this)
{
	return cc_token_type_is_number(cc_token_type(this));
}

static inline
bool cc_token_is_storage_specifier(const struct cc_token *this)
{
	return cc_token_type_is_storage_specifier(cc_token_type(this));
}

static inline
bool cc_token_is_type_specifier(const struct cc_token *this)
{
	return cc_token_type_is_type_specifier(cc_token_type(this));
}

static inline
bool cc_token_is_type_qualifier(const struct cc_token *this)
{
	return cc_token_type_is_type_qualifier(cc_token_type(this));
}

static inline
bool cc_token_is_alignment_specifier(const struct cc_token *this)
{
	return cc_token_type_is_alignment_specifier(cc_token_type(this));
}

static inline
bool cc_token_is_function_specifier(const struct cc_token *this)
{
	return cc_token_type_is_function_specifier(cc_token_type(this));
}
void	cc_token_delete(void *this);
/*****************************************************************************/
struct cc_token_stream {
	const char	*buffer;	/* file containing the cpp_tokens */
	size_t		buffer_size;
	size_t		position;
	struct ptr_queue	q;
};

static inline
void cc_token_stream_init(struct cc_token_stream *this,
						  const char *buffer,
						  const size_t buffer_size)
{
	this->buffer = buffer;
	this->buffer_size = buffer_size;
	this->position = 0;
	ptrq_init(&this->q, cc_token_delete);
	/* Each entry in the queue is a pointer. */
}
#if 0
static inline
bool cc_token_stream_is_empty(const struct cc_token_stream *this)
{
	return ptrq_is_empty(&this->q);
}

static inline
err_t cc_token_stream_add_tail(struct cc_token_stream *this,
							   struct cc_token *token)
{
	return ptrq_add_tail(&this->q, token);
}
#endif
static inline
err_t cc_token_stream_add_head(struct cc_token_stream *this,
							   struct cc_token *token)
{
	return ptrq_add_head(&this->q, token);
}

static inline
void cc_token_stream_empty(struct cc_token_stream *this)
{
	ptrq_empty(&this->q);
}
#endif
