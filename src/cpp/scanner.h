/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef SRC_CPP_SCANNER_H
#define SRC_CPP_SCANNER_H

#include <inc/cpp/scanner.h>
#include "lexer.h"

#include <inc/types.h>
#include <stdint.h>

struct cpp_token {
	struct lexer_token	*base;
	bool	is_marked;
	bool	has_white_space;
	bool	is_first;
};

static inline
const char *cpp_token_source(const struct cpp_token *this)
{
	return lexer_token_source(this->base);
}

static inline
size_t cpp_token_source_length(const struct cpp_token *this)
{
	return lexer_token_source_length(this->base);
}

static inline
const char *cpp_token_resolved(const struct cpp_token *this)
{
	return lexer_token_resolved(this->base);
}

static inline
size_t cpp_token_resolved_length(const struct cpp_token *this)
{
	return lexer_token_resolved_length(this->base);
}

static inline
enum lexer_token_type cpp_token_type(const struct cpp_token *this)
{
	return lexer_token_type(this->base);
}

static inline
int cpp_token_is_string_literal(const struct cpp_token *this)
{
	return lexer_token_is_string_literal(this->base);
}

static inline
int cpp_token_is_char_const(const struct cpp_token *this)
{
	return lexer_token_is_char_const(this->base);
}

static inline
int cpp_token_is_identifier(const struct cpp_token *this)
{
	return lexer_token_is_identifier(this->base);
}

static inline
int cpp_token_is_first(const struct cpp_token *this)
{
	return lexer_token_is_first(this->base);
}

static inline
int cpp_token_has_white_space(const struct cpp_token *this)
{
	return this->has_white_space;
}

static inline
bool cpp_token_is_marked(const struct cpp_token *this)
{
	return this->is_marked;
}

static
void cpp_token_delete(void *p);
/*****************************************************************************/
struct cpp_token_stream {
	struct lexer	*lexer;
	struct queue	tokens;
};

static inline
void cpp_token_stream_init(struct cpp_token_stream *this,
						   struct lexer *lexer)
{
	this->lexer = lexer;
	queue_init(&this->tokens, cpp_token_delete);
}

static inline
bool cpp_token_stream_is_empty(const struct cpp_token_stream *this)
{
	return queue_is_empty(&this->tokens);
}

static inline
err_t cpp_token_stream_add_tail(struct cpp_token_stream *this,
								struct cpp_token *token)
{
	return queue_add_tail(&this->tokens, token);
}

static inline
err_t cpp_token_stream_add_head(struct cpp_token_stream *this,
								struct cpp_token *token)
{
	return queue_add_head(&this->tokens, token);
}
/*****************************************************************************/
struct macro {
	struct cpp_token	*identifier;
	struct queue	parameters;
	struct queue	replacement_list;
	bool	is_function_like;
	bool	is_variadic;
};
/*****************************************************************************/
/* conditional inclusion stack */

/*
 * wait: skip the current region, and wait for the next.
 * scan: scan the current region.
 * done: done. do not scan or wait.
 *
 * if the IF condition is false, we start in wait.
 * if the IF condition is true, we start in scan.
 *
 * if an ancestor-construct is in wait/done state, all its child constructs
 * are set to done.
 */
enum cond_incl_state {
	COND_INCL_STATE_WAIT,
	COND_INCL_STATE_SCAN,
	COND_INCL_STATE_DONE,
};

struct cond_incl_stack_entry {
	enum lexer_token_type	type;	/* only if and else. elif == if */
	enum cond_incl_state	state;
};
/*****************************************************************************/
/* low-value, high-precedence */
struct rpn_operator_precedence {
	enum lexer_token_type	operator;
	int	precedence;
};

/* only binary ops */
static const struct rpn_operator_precedence g_rpn_operator_precedence[] = {
	{LXR_TOKEN_UNARY_MINUS,	0},
	{LXR_TOKEN_TILDE,	0},
	{LXR_TOKEN_EXCLAMATION_MARK,	0},

	{LXR_TOKEN_MUL,			1},
	{LXR_TOKEN_DIV,			1},
	{LXR_TOKEN_MOD,			1},

	{LXR_TOKEN_PLUS,		2},
	{LXR_TOKEN_MINUS,		2},

	{LXR_TOKEN_SHIFT_LEFT,	3},
	{LXR_TOKEN_SHIFT_RIGHT,	3},

	{LXR_TOKEN_LESS_THAN,			4},
	{LXR_TOKEN_GREATER_THAN,		4},
	{LXR_TOKEN_LESS_THAN_EQUALS,	4},
	{LXR_TOKEN_GREATER_THAN_EQUALS,	4},

	{LXR_TOKEN_EQUALS,		5},
	{LXR_TOKEN_NOT_EQUALS,	5},

	{LXR_TOKEN_BITWISE_AND,	6},

	{LXR_TOKEN_BITWISE_XOR,	7},

	{LXR_TOKEN_BITWISE_OR,	8},

	{LXR_TOKEN_LOGICAL_AND,	9},

	{LXR_TOKEN_LOGICAL_OR,	10},

	{LXR_TOKEN_QUESTION_MARK,	11},

	/* Colon is treated as right-associative */
	{LXR_TOKEN_COLON,	12},
};

enum rpn_stack_entry_type
{
	RPN_STACK_ENTRY_OPERATOR,
	RPN_STACK_ENTRY_SIGNED,
	RPN_STACK_ENTRY_UNSIGNED,
};

static inline
int rpn_operator_precedence(const enum lexer_token_type operator)
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(g_rpn_operator_precedence); ++i) {
		if (g_rpn_operator_precedence[i].operator == operator)
			return g_rpn_operator_precedence[i].precedence;
	}
	assert(0);
	return -1;
}

struct rpn_stack_entry {
	enum rpn_stack_entry_type	type;
	union {
		enum lexer_token_type		operator;
		uintmax_t	value;	/* the std says these nums are [u]intmax_t */
	} u;
};
/*****************************************************************************/
struct scanner {
	struct array	macros;
	struct queue	cistk;	/* conditional inclusion stack */

	const char	*include_paths[4];
	const char	*predefined_macros_path;
	const char	*cpp_tokens_path;
	int			cpp_tokens_fd;

	int	include_path_lens[4];
	bool	is_running_predefined_macros;
};
#endif
