/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef INC_CPP_LEXER_H
#define INC_CPP_LEXER_H

#include <inc/errno.h>

#include <stdbool.h>
#include <assert.h>
#include <uchar.h>
#include <sys/types.h>

enum lexer_token_type {
#define DEF(t)	LXR_TOKEN_ ## t,
#include <inc/cpp/tokens.h>
#undef DEF
};

struct lexer_position {
	off_t	lex_pos;
	off_t	file_row;
	off_t	file_col;
};

enum char_const_escape_type {
	CHAR_CONST_ESCAPE_NONE,
	CHAR_CONST_ESCAPE_SIMPLE,
	CHAR_CONST_ESCAPE_HEX,
	CHAR_CONST_ESCAPE_OCT,
	CHAR_CONST_ESCAPE_UCN_4,
	CHAR_CONST_ESCAPE_UCN_8,
};

/* The position of the token is separate property */
struct lexer_token {
	enum lexer_token_type	type;

	size_t	lex_size;

	/* The actual source bytes from the src file */
	const char *source;
	size_t	source_len;

	/*
	 * each esc-seqs in source resolved to the corresponding character.
	 * Only done upfront for token_identifiers because it is possible that the
	 * macro name may not have any esc-seq, but its invocation is made with its
	 * name containing an esc-seq. For char-const, the value should be enough.
	 * For strings, we wait until translation-phase 5.
	 * If source doesn't contain any esc-seq, resolved points to source.
	 */
	const char *resolved;
	size_t	resolved_len;

	/* does it have at least one non-nl ws before it? */
	bool	has_white_space;
	bool	is_first;	/* first non-ws token on a new line */
	int		ref_count;
};

static inline
void lexer_token_ref(struct lexer_token *this)
{
	++this->ref_count;
}

static inline
int lexer_token_has_white_space(const struct lexer_token *this)
{
	return this->has_white_space;
}

static inline
enum lexer_token_type lexer_token_type(const struct lexer_token *this)
{
	return this->type;
}

static inline
const char *lexer_token_resolved(const struct lexer_token *this)
{
	return this->resolved;
}

static inline
int lexer_token_lex_size(const struct lexer_token *this)
{
	return this->lex_size;
}

static inline
int lexer_token_resolved_length(const struct lexer_token *this)
{
	return this->resolved_len;
}

static inline
const char *lexer_token_source(const struct lexer_token *this)
{
	return this->source;
}

static inline
int lexer_token_source_length(const struct lexer_token *this)
{
	return this->source_len;
}

static inline
bool lexer_token_is_first(const struct lexer_token *this)
{
	return this->is_first;
}

static inline
bool lexer_token_is_identifier(const struct lexer_token *this)
{
	enum lexer_token_type type = lexer_token_type(this);
	return type >= LXR_TOKEN_IDENTIFIER && type <= LXR_TOKEN_DIRECTIVE_WARNING;
}

static inline
bool lexer_token_is_string_literal(const struct lexer_token *this)
{
	enum lexer_token_type type = lexer_token_type(this);
	return (type >= LXR_TOKEN_CHAR_STRING_LITERAL &&
			type <= LXR_TOKEN_WCHAR_T_STRING_LITERAL);
}

static inline
bool lexer_token_is_char_const(const struct lexer_token *this)
{
	enum lexer_token_type type = lexer_token_type(this);
	return (type >= LXR_TOKEN_INTEGER_CHAR_CONST &&
			type <= LXR_TOKEN_WCHAR_T_CHAR_CONST);
}

static inline
bool lexer_token_is_key_word(const struct lexer_token *this)
{
	enum lexer_token_type type = lexer_token_type(this);
	return (type >= LXR_TOKEN_ATOMIC &&
			type <= LXR_TOKEN_DIRECTIVE_WARNING);
}

static inline
bool lexer_token_is_punctuator(const struct lexer_token *this)
{
	enum lexer_token_type type = lexer_token_type(this);
	return (type >= LXR_TOKEN_LEFT_BRACE &&
			type <= LXR_TOKEN_ELLIPSIS);
}

void	lexer_token_init(struct lexer_token *this);
void	lexer_token_deref(struct lexer_token *this);
err_t	lexer_token_evaluate_char_const(const struct lexer_token *this,
										char32_t *out);
/*****************************************************************************/
struct lexer;
err_t	lexer_new(const char *path,	/* Either path, or buffer, but not both */
				  const char *buffer,
				  const off_t buffer_size,
				  struct lexer **out);
err_t	lexer_delete(struct lexer *this);
err_t	lexer_lex_token(struct lexer *this,
						struct lexer_token **out);
#endif
