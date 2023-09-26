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
bool cpp_token_is_key_word(const struct cpp_token *this)
{
	return lexer_token_is_key_word(this->base);
}

static inline
bool cpp_token_is_punctuator(const struct cpp_token *this)
{
	return lexer_token_is_punctuator(this->base);
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
struct cpp_tokens {
	struct ptr_queue	q;
};

#define CPP_TOKENS_FOR_EACH(cts, ix, e)	\
	PTRQ_FOR_EACH(&(cts)->q, ix, e)
#define CPP_TOKENS_FOR_EACH_WITH_REMOVE(cts, e)	\
	PTRQ_FOR_EACH_WITH_REMOVE(&(cts)->q, e)
#define CPP_TOKENS_FOR_EACH_REVERSE(cts, ix, e)	\
	PTRQ_FOR_EACH_REVERSE(&(cts)->q, ix, e)
#define CPP_TOKENS_FOR_EACH_WITH_REMOVE_REVERSE(cts, e)	\
	PTRQ_FOR_EACH_WITH_REMOVE_REVERSE(&(cts)->q, e)

static inline
void cpp_tokens_init(struct cpp_tokens *this)
{
	ptrq_init(&this->q, cpp_token_delete);
}

static inline
bool cpp_tokens_is_empty(const struct cpp_tokens *this)
{
	return ptrq_is_empty(&this->q);
}

static inline
int cpp_tokens_num_entries(const struct cpp_tokens *this)
{
	return ptrq_num_entries(&this->q);
}

static inline
struct cpp_token *cpp_tokens_peek_entry(const struct cpp_tokens *this,
										const int index)
{
	return ptrq_peek_entry(&this->q, index);
}

static inline
struct cpp_token *cpp_tokens_peek_head(const struct cpp_tokens *this)
{
	return ptrq_peek_head(&this->q);
}

static inline
struct cpp_token *cpp_tokens_peek_tail(const struct cpp_tokens *this)
{
	return ptrq_peek_tail(&this->q);
}

static inline
err_t cpp_tokens_add_head(struct cpp_tokens *this,
						  struct cpp_token *token)
{
	return ptrq_add_head(&this->q, token);
}

static inline
err_t cpp_tokens_add_tail(struct cpp_tokens *this,
						  struct cpp_token *token)
{
	return ptrq_add_tail(&this->q, token);
}

static inline
struct cpp_token *cpp_tokens_remove_head(struct cpp_tokens *this)
{
	return ptrq_remove_head(&this->q);
}

static inline
struct cpp_token *cpp_tokens_remove_tail(struct cpp_tokens *this)
{
	return ptrq_remove_tail(&this->q);
}

static inline
void cpp_tokens_delete_head(struct cpp_tokens *this)
{
	ptrq_delete_head(&this->q);
}

static inline
err_t cpp_tokens_move(struct cpp_tokens *this,
					  struct cpp_tokens *to)
{
	return ptrq_move(&this->q, &to->q);
}

static inline
void cpp_tokens_empty(struct cpp_tokens *this)
{
	ptrq_empty(&this->q);
}
/*****************************************************************************/
struct cpp_token_stream {
	struct lexer		*lexer;
	struct cpp_tokens	tokens;
};

static inline
void cpp_token_stream_init(struct cpp_token_stream *this,
						   struct lexer *lexer)
{
	this->lexer = lexer;
	cpp_tokens_init(&this->tokens);
}

static inline
bool cpp_token_stream_is_empty(const struct cpp_token_stream *this)
{
	return cpp_tokens_is_empty(&this->tokens);
}

static inline
err_t cpp_token_stream_add_tail(struct cpp_token_stream *this,
								struct cpp_token *token)
{
	return cpp_tokens_add_tail(&this->tokens, token);
}

static inline
err_t cpp_token_stream_add_head(struct cpp_token_stream *this,
								struct cpp_token *token)
{
	return cpp_tokens_add_head(&this->tokens, token);
}
/*****************************************************************************/
struct macro {
	struct cpp_token	*identifier;
	struct cpp_tokens	parameters;
	struct cpp_tokens	replacement_list;
	bool	is_function_like;
	bool	is_variadic;
};

static
void	macro_delete(void *p);
/*****************************************************************************/
struct macro_stack {
	struct ptr_queue	q;
};

static inline
void macro_stack_init(struct macro_stack *this)
{
	ptrq_init(&this->q, NULL);
}

static inline
bool macro_stack_find(const struct macro_stack *this,
					  const struct macro *macro)
{
	return ptrq_find(&this->q, macro);
}

static inline
err_t macro_stack_push(struct macro_stack *this,
					   struct macro *macro)
{
	return ptrq_add_tail(&this->q, macro);
}

static inline
struct macro *macro_stack_peek(const struct macro_stack *this)
{
	return ptrq_peek_tail(&this->q);
}

static inline
struct macro *macro_stack_pop(struct macro_stack *this)
{
	return ptrq_remove_tail(&this->q);
}
/*****************************************************************************/
struct macros {
	struct ptr_queue	q;
};

#define MACROS_FOR_EACH(ms, ix, e)	PTRQ_FOR_EACH(&((ms)->q), ix, e)

static inline
void macros_init(struct macros *this)
{
	ptrq_init(&this->q, macro_delete);
}

static inline
void macros_empty(struct macros *this)
{
	ptrq_empty(&this->q);
}

static inline
void macros_delete_entry(struct macros *this,
						 const int index)
{
	ptrq_delete_entry(&this->q, index);
}

static inline
err_t macros_add_tail(struct macros *this,
					  struct macro *macro)
{
	return ptrq_add_tail(&this->q, macro);
}

static inline
struct macro *macros_peek_entry(const struct macros *this,
								const int index)
{
	return ptrq_peek_entry(&this->q, index);
}
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

struct cond_incl_stack {
	struct val_queue	q;
};

static inline
void cond_incl_stack_init(struct cond_incl_stack *this)
{
	valq_init(&this->q, sizeof(struct cond_incl_stack_entry), NULL);
}

static inline
bool cond_incl_stack_is_empty(const struct cond_incl_stack *this)
{
	return valq_is_empty(&this->q);
}

static inline
int cond_incl_stack_num_entries(const struct cond_incl_stack *this)
{
	return valq_num_entries(&this->q);
}

static inline
err_t cond_incl_stack_push(struct cond_incl_stack *this,
						   const struct cond_incl_stack_entry *entry)
{
	return valq_add_tail(&this->q, entry);
}

static inline
struct cond_incl_stack_entry cond_incl_stack_peek(const struct cond_incl_stack *this)
{
	struct cond_incl_stack_entry entry;
	entry = *(struct cond_incl_stack_entry *)valq_peek_tail(&this->q);
	return entry;
}

static inline
struct cond_incl_stack_entry cond_incl_stack_pop(struct cond_incl_stack *this)
{
	struct cond_incl_stack_entry entry = cond_incl_stack_peek(this);
	valq_remove_tail(&this->q);
	return entry;
}
/*****************************************************************************/
/* low-value, high-precedence */
struct rpn_operator_precedence {
	enum lexer_token_type	operator;
	int	precedence;
};

/* only binary ops */
static const struct rpn_operator_precedence g_rpn_operator_precedence[] = {
	{LXR_TOKEN_UNARY_MINUS,	0},
	{LXR_TOKEN_BITWISE_NOT,	0},
	{LXR_TOKEN_LOGICAL_NOT,	0},

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

	{LXR_TOKEN_CONDITIONAL,	11},

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

struct rpn_stack {
	struct val_queue	q;
};

#define RPN_STACK_FOR_EACH_WITH_REMOVE_REVERSE(stk, e)	\
	VALQ_FOR_EACH_WITH_REMOVE_REVERSE(&(stk)->q, e)

static inline
void rpn_stack_init(struct rpn_stack *this)
{
	valq_init(&this->q, sizeof(struct rpn_stack_entry), NULL);
}

static inline
bool rpn_stack_is_empty(const struct rpn_stack *this)
{
	return valq_is_empty(&this->q);
}

static inline
err_t rpn_stack_push(struct rpn_stack *this,
					 const struct rpn_stack_entry *entry)
{
	return valq_add_tail(&this->q, entry);
}

static inline
struct rpn_stack_entry rpn_stack_peek(const struct rpn_stack *this)
{
	struct rpn_stack_entry entry;
	entry = *(struct rpn_stack_entry *)valq_peek_tail(&this->q);
	return entry;
}

static inline
struct rpn_stack_entry rpn_stack_pop(struct rpn_stack *this)
{
	struct rpn_stack_entry entry = rpn_stack_peek(this);
	valq_remove_tail(&this->q);
	return entry;
}
/*****************************************************************************/
struct scanner {
	struct macros	macros;
	struct cond_incl_stack	cistk;

	const char	*include_paths[4];
	const char	*predefined_macros_path;
	const char	*cpp_tokens_path;
	int			cpp_tokens_fd;

	int	include_path_lens[4];
	bool	is_running_predefined_macros;
};
#endif
