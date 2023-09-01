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

static inline
bool cc_token_type_is_non_terminal(const enum cc_token_type this)
{
	return this >= CC_TOKEN_TRANSLATION_OBJECT;
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
	return this == CC_TOKEN_IDENTIFIER || cc_token_type_is_key_word(this);
}

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
struct cc_grammar_derivation {
	int	item_set;
	int	item;	/* index into the reduce-items of the item-set */
};

/*
 * element, rule, origin uniquely identifies an item.
 * We can therefore place array of back pointers here.
 * A new base-item is created only during prediction.
 *
 * When a completion is processed, the item that is added has its dot-pos
 * moved, and a new back-ptr is added.
 *
 * When a terminal is scanned, the base-item remains the same, the dot-pos
 * moves across the terminal.
 */
struct cc_grammar_element;
struct cc_grammar_item_base {
	/* the lhs non-term */
	struct cc_grammar_element *element;
	int	rule;		/* index into ge.rules */
	/* The item-set where the item with dot-pos 0 was added by predn */
	int	origin;
	struct val_queue	back;
};

void cc_grammar_item_base_delete(void *p);

static inline
void cc_grammar_item_base_init(struct cc_grammar_item_base *this,
							   struct cc_grammar_element *element,
							   const int rule,
							   const int origin)
{
	this->element = element;
	this->rule = rule;
	this->origin = origin;
	valq_init(&this->back, sizeof(struct cc_grammar_derivation), NULL);
}

/* an element can be either a terminal or a non-terminal. */
struct cc_grammar_rule {
	struct val_queue	elements;
	/*
	 * Maintain in incr. order of origin, so that we can employ binary search
	 * algorithm
	 */
	struct ptr_queue	base_items;	/* [element,rule,origin] */
};

void cc_grammar_rule_delete(void *p);

/* Only non-terminals */
struct cc_grammar_element {
	enum cc_token_type	type;
	struct val_queue	rules;
};

/*
 * If some structure takes a pointer to an entry in the valq, the next time the
 * valq is realloc'd, the pointer may become invalid as realloc may reallocate
 * the entire valq into another region.
 */
struct cc_grammar_item {
	struct cc_grammar_item_base *base;
	int	dot_position;	/* 0 <= dot-pos <= rule.num_entries */
};

static
void cc_grammar_item_delete(void *p)
{
	(void)p;
}

/* Earley item-set. TODO McLean/Horspool */
struct cc_grammar_item_set {
	int index;	/* May need to change it to 64-bit */
	struct val_queue	reduce_items;
	struct val_queue	shift_items;
	struct cc_token		*token;
	/*
	 * Token is valid for item-set-#1 and above. It is stored here until the
	 * parse is complete, after which it will become part of the parse tree.
	 */
};

static inline
void cc_grammar_item_set_init(struct cc_grammar_item_set *this,
							  const int index)
{
	size_t size = sizeof(struct cc_grammar_item);
	valq_init(&this->reduce_items, size, cc_grammar_item_delete);
	valq_init(&this->shift_items, size, cc_grammar_item_delete);
	this->token = NULL;
	this->index = index;
}

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
	struct val_queue	elements;	/* Only non-terminals */
	struct val_queue	item_sets;

	int	cpp_tokens_fd;
	const char	*cpp_tokens_path;
	struct cc_token_stream	stream;
};
err_t cc_load_grammar(struct compiler *this);
#endif
