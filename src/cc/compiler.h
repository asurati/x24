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
	struct ptr_queue	q;
};

static inline
void cc_token_stream_init(struct cc_token_stream *this,
						  const char *buffer,
						  const size_t buffer_size)
{
	this->buffer = buffer;
	this->buffer_size = buffer_size;
	ptrq_init(&this->q, cc_token_delete);
	/* Each entry in the queue is a pointer. */
}

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
/*****************************************************************************/
struct cc_grammar_rule {
	struct val_queue	elements;	/* the first int is lhs, rest is rhs */
};

void cc_grammar_rule_delete(void *p);

/* terminals + non-terminals. rules valid for non-terminals alone */
struct cc_grammar_element {
	enum cc_token_type	type;
	struct val_queue	rules;
};

struct cc_grammar_item {
	int	element;
	int	rule;
	int	dot_position;	/* 0 <= dot-pos <= rule.num_entries */
	int	origin;
	/*
	 * origin is the item-set where the corresponding item with dot-pos == 0
	 * was added by the prediction.
	 */
};

/*
 * Earley item-set.
 * As long as any item still points to the item-set, we cannot free it.
 * items whose origin is the same as the item-set do not contribute to the
 * ref-count.
 * TODO McLean/Horspool impl to combine lr(1) and Earley.
 */
struct cc_grammar_item_set {
	struct ptr_queue	items;	/* Each q-entry is cc_gram_item* */
};

/*
 * token is meant to store token that need its string info to
 * identify themselves; for e.g. identifiers, numbers, strings, etc.
 * key-words, puntuators do not need to store their tokens.
 */
struct cc_parse_node {
	enum cc_token_type	type;
	struct cc_token		*token;
	struct ptr_queue	child_nodes;	/* each q-entry is a cc_parse_node* */
};
/*****************************************************************************/
struct compiler {
	struct val_queue	elements;
	struct val_queue	item_sets;

	int	cpp_tokens_fd;
	const char	*cpp_tokens_path;
	struct cc_token_stream	stream;
};
err_t cc_load_grammar(struct compiler *this);
#endif
