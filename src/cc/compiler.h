/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef SRC_CC_COMPILER_H
#define SRC_CC_COMPILER_H

#include <inc/cc/compiler.h>

#include <inc/types.h>

/*
 * Only terminals, including char-consts, string-literals, integer-consts,
 * floating-consts, punctuators, keywords and identifiers.
 */
enum cc_token_type {
#define DEF(t)	CC_TOKEN_ ## t,
#include <inc/cpp/tokens.h>	/* grammar terminals */
#include <inc/cc/tokens.h>	/* grammar non-terminals */
#undef DEF
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
	return this == CC_TOKEN_IDENTIFIER || cc_token_type_is_key_word(this);
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
void cc_token_delete(void *this);
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

static inline
err_t cc_token_stream_add_head(struct cc_token_stream *this,
							   struct cc_token *token)
{
	return ptrq_add_head(&this->q, token);
}
#endif
static inline
void cc_token_stream_empty(struct cc_token_stream *this)
{
	ptrq_empty(&this->q);
}
/*****************************************************************************/
struct cc_type;
struct cc_node {
	enum cc_token_type	type;
	/*
	 * When cc_token_type is used in a cc_token, it represents the C token type
	 * - grammatical terminals and non-terminals.
	 * When cc_token_type is used in a cc_node, it represents the type of an
	 * AST node.
	 */

	/*
	 * For identifiers, this is the cpp's resolved name. i.e. any esc-seqs in
	 * the original src were resolved to the corresponding src-char-set-encoded
	 * byte stream (src-char-set is assume to be utf-8.)
	 * That stream is stored here.
	 *
	 * For string-literals and char-consts, this is their exec-char-set
	 * representation.
	 *
	 * For numbers too.
	 *
	 * For keywords and punctuators it is null.
	 */
	const char	*string;
	size_t		string_len;	/* doesn't include the nul char */

	/*
	 * For e.g. a node of type CC_NODE_PLUS (binary +) will report the
	 * type of addition here (int, char, float, bit-int, etc).
	 */
	struct cc_type		*out_type;
	struct ptr_queue	child_nodes;	/* each q-entry is a cc_node* */
};

extern void cc_node_delete(void *p);

static inline
void cc_node_init(struct cc_node *this)
{
	this->type = CC_TOKEN_INVALID;
	this->string = NULL;
	this->out_type = NULL;
	this->string_len = 0;
	ptrq_init(&this->child_nodes, cc_node_delete);
}

static inline
enum cc_token_type cc_node_type(const struct cc_node *this)
{
	return this->type;
}

static inline
int cc_node_num_children(const struct cc_node *this)
{
	return ptrq_num_entries(&this->child_nodes);
}

static inline
err_t cc_node_add_head_child(struct cc_node *this,
							 struct cc_node *child)
{
	return ptrq_add_head(&this->child_nodes, child);
}

static inline
err_t cc_node_add_tail_child(struct cc_node *this,
							 struct cc_node *child)
{
	return ptrq_add_tail(&this->child_nodes, child);
}

static inline
struct cc_node *
cc_node_peek_child_node(const struct cc_node *this,
						const int index)
{
	return ptrq_peek_entry(&this->child_nodes, index);
}

static inline
struct cc_node *
cc_node_peek_head_child(const struct cc_node *this)
{
	return cc_node_peek_child_node(this, 0);
}

static inline
struct cc_node *
cc_node_peek_tail_child(const struct cc_node *this)
{
	int num_children = cc_node_num_children(this);
	return cc_node_peek_child_node(this, num_children - 1);
}

static inline
struct cc_node *
cc_node_remove_child_node(struct cc_node *this,
						  const int index)
{
	return ptrq_remove_entry(&this->child_nodes, index);
}

static inline
struct cc_node *
cc_node_remove_head_child(struct cc_node *this)
{
	return cc_node_remove_child_node(this, 0);
}

static inline
struct cc_node *
cc_node_remove_tail_child(struct cc_node *this)
{
	int num_children = cc_node_num_children(this);
	return cc_node_remove_child_node(this, num_children - 1);
}

static inline
err_t cc_node_move_children(struct cc_node *this,
							struct cc_node *to)
{
	return ptrq_move(&this->child_nodes, &to->child_nodes);
}

static inline
bool cc_node_is_key_word(const struct cc_node *this)
{
	return cc_token_type_is_key_word(cc_node_type(this));
}

static inline
bool cc_node_is_punctuator(const struct cc_node *this)
{
	return cc_token_type_is_punctuator(cc_node_type(this));
}

static inline
bool cc_node_is_identifier(const struct cc_node *this)
{
	return cc_token_type_is_identifier(cc_node_type(this));
}

static inline
bool cc_node_is_string_literal(const struct cc_node *this)
{
	return cc_token_type_is_string_literal(cc_node_type(this));
}

static inline
bool cc_node_is_char_const(const struct cc_node *this)
{
	return cc_token_type_is_char_const(cc_node_type(this));
}

static inline
bool cc_node_is_number(const struct cc_node *this)
{
	return cc_token_type_is_number(cc_node_type(this));
}
/*****************************************************************************/
enum cc_type_type {
	CC_TYPE_INVALID,

	CC_TYPE_BOOL,	/* is always unsigned */

	CC_TYPE_SIGNED_CHAR,	/* char and signed char are different */

	CC_TYPE_CHAR,	/* These are always signed */
	CC_TYPE_SHORT,
	CC_TYPE_INT,
	CC_TYPE_LONG,
	CC_TYPE_LONG_LONG,
	CC_TYPE_BIT_INT,
	CC_TYPE_BIT_FIELD,

	CC_TYPE_FLOAT,
	CC_TYPE_DOUBLE,
	CC_TYPE_LONG_DOUBLE,
	CC_TYPE_DECIMAL_32,
	CC_TYPE_DECIMAL_64,
	CC_TYPE_DECIMAL_128,
	CC_TYPE_COMPLEX,

	CC_TYPE_ENUMERATION,
	CC_TYPE_STRUCTURE,
	CC_TYPE_UNION,
	CC_TYPE_ARRAY,
	CC_TYPE_POINTER,
	CC_TYPE_FUNCTION,
	CC_TYPE_VOID,
	CC_TYPE_ATOMIC,

	/* qualifiers to qualify type take the same form as type */
	CC_TYPE_QUALIFIER_CONST,
	CC_TYPE_QUALIFIER_RESTRICT,
	CC_TYPE_QUALIFIER_VOLATILE,
	CC_TYPE_QUALIFIER_ATOMIC,

	/* Non-grammatical. Make unsigned a qualifier */
	CC_TYPE_QUALIFIER_UNSIGNED,
};

/*
 * We assume that sizeof(int) >= 4 bytes = 32 bits.
 * alignments are represented as values of type size_t. But we use int here as
 * alignof(max_align_t) is 0x10, which is representable in an int.
 * When an ast-node for alignof(int), for e.g., is build, the node's out_type
 * is set to size_t, as alignof() returns its value in size_t type.
 */
struct cc_type_integer {
	int		width;		/* in bits. padding + value + sign */
	int		precision;	/* in bits. value bits */
	int		padding;	/* in bits. */
	int		alignment;	/* in bits. */
	bool	is_signed;
};

struct cc_type_bit_field {
	struct cc_type	*type;	/* The underlying type int or bool */
	int width;
	int	offset;
};

struct cc_type_pointer {
	struct cc_type		*referenced_type;
};

/* Dont care about names of func/params */
struct cc_type_function {
	struct cc_type		*return_type;
	struct ptr_queue	parameter_types;
};

struct cc_type_array {
	struct cc_type	*element_type;
	int	num_elements;	/* Should it be a cc_node? */
	bool is_vla;
};

/* enum tag is part of symbol table. */
struct cc_type_enumeration {
	struct cc_type		*type;	/* type of the values */
	struct ptr_queue	names;		/* cc_node * */
	struct ptr_queue	values;	/* cc_node * */
	bool	is_type_fixed;
};

/*
 * struct/union tag is part of symbol table.
 * These have name-type pairs for members.
 */
struct cc_type_struct_union {
	struct ptr_queue	names;		/* cc_node * */
	struct ptr_queue	types;		/* cc_type * */
};

/* There is a tree per type */
struct cc_type {
	struct cc_type		*parent;
	struct ptr_queue	children;
	enum cc_type_type	type;
	union {
		struct cc_type_integer		integer;
		struct cc_type_bit_field	bit_field;
		struct cc_type_function		function;
		struct cc_type_array		array;
		struct cc_type_pointer		pointer;
		struct cc_type_enumeration	enumeration;
		struct cc_type_struct_union	struct_union;
	} u;
};
/*****************************************************************************/
struct compiler {
	struct cc_node		*root;
	struct ptr_queue	types;

	int	cpp_tokens_fd;
	const char	*cpp_tokens_path;
	struct cc_token_stream	stream;
};
#endif
