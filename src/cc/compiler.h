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

static inline
bool cc_token_type_is_storage_class_specifier(const enum cc_token_type this)
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
bool cc_token_is_storage_class_specifier(const struct cc_token *this)
{
	return cc_token_type_is_storage_class_specifier(cc_token_type(this));
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
/* Only relevant for identfiers */
enum cc_linkage_type {
	CC_LINKAGE_NONE,
	CC_LINKAGE_EXTERNAL,
	CC_LINKAGE_INTERNAL,
};

/* Only relevant for identfiers */
enum cc_name_space_type {
	CC_NAME_SPACE_LABEL,		/* Within a function */
	CC_NAME_SPACE_TAG,		/* struct/union/enum */
	CC_NAME_SPACE_MEMBER,	/* struct/union */
	CC_NAME_SPACE_ATTRIBUTE_PREFIXES,	/* + std. attr */
	CC_NAME_SPACE_ATTRIBUTE_PREFIXED_ATTRIBUTE,
	CC_NAME_SPACE_ORDINARY,
};

struct cc_symbol_table;
struct cc_node_identifier {
	/*
	 * This is the cpp's resolved name. i.e. any esc-seqs in the original src
	 * were resolved to the corresponding src-char-set-encoded byte stream
	 * (src-char-set is assume to be utf-8.) That stream is stored here.
	 *
	 * If the identifier is a key-word, this is NULL.
	 */
	const char	*string;
	size_t		string_len;	/* len doesn't include the terminating nul */

	/*
	 * file/global scope
	 * block scope (includes functionbody)
	 * func-prototype scope (for both func decl and def)
	 */
	struct cc_symbol_table	*scope;

	/* storage duration of the object that this identifier locates. */
	enum cc_token_type		storage;
	enum cc_linkage_type	linkage;
	enum cc_name_space_type	name_space;
};

struct cc_node_string_literal {
	/* exec-char-set representation */
	const char	*string;
	size_t		string_len;	/* len doesn't include the terminating nul */
};

struct cc_node_char_const {
	/* exec-char-set representation */
	const char	*string;
	size_t		string_len;	/* len doesn't include the terminating nul */
};

struct cc_node_number {
	/* literal */
	const char	*string;
	size_t		string_len;	/* len doesn't include the terminating nul */
};

struct cc_type;
struct cc_node {
	struct ptr_tree		tree;	/* rooted at this node */
	enum cc_token_type	type;
	/*
	 * When cc_token_type is used in a cc_token, it represents the C token type
	 * - grammatical terminals and non-terminals.
	 * When cc_token_type is used in a cc_node, it represents the type of an
	 * AST node.
	 */

	/*
	 * For e.g. a node of type CC_NODE_PLUS (binary +) will report the
	 * type of addition here (int, char, float, bit-int, etc).
	 * For statements and other constructs, the type is void, represented
	 * by a out_type == NULL.
	 */
	struct cc_type		*out_type;
	union {
		struct cc_node_identifier		identifier;
		struct cc_node_string_literal	string_literal;
		struct cc_node_char_const		char_const;
		struct cc_node_number			number;
	} u;
};

static void cc_node_delete(void *p);

static inline
void cc_node_init(struct cc_node *this)
{
	this->type = CC_TOKEN_INVALID;
	this->out_type = NULL;
	ptrt_init(&this->tree, cc_node_delete);
}

static inline
enum cc_token_type cc_node_type(const struct cc_node *this)
{
	return this->type;
}

static inline
err_t cc_node_add_tail_child(struct cc_node *this,
							 struct cc_node *child)
{
	return ptrt_add_tail_child(&this->tree, child);
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
	/* CC_TYPE_QUALIFIER_UNSIGNED, */
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
	int		num_elements;	/* Should it be a cc_node? */
	bool	is_vla;
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
 * the cc_type_type differentiates b/w struct and union.
 */
struct cc_type_struct_union {
	struct ptr_queue	names;		/* cc_node * */
	struct ptr_queue	types;		/* cc_type * */
};

/* There is a tree per type */
struct cc_type {
	struct ptr_tree		tree;
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
/*
 * sym-tab stores name-type pairs for objects, funcs, typedefs. It also stores
 * the type-tree for each root type.
 */
enum cc_symbol_table_entry_type {
	CC_SYMBOL_TABLE_ENTRY_INVALID,
	CC_SYMBOL_TABLE_ENTRY_OBJECT,
	CC_SYMBOL_TABLE_ENTRY_FUNCTION,
	CC_SYMBOL_TABLE_ENTRY_TYPE_DEF,
	CC_SYMBOL_TABLE_ENTRY_TYPE_TREE,
	/*
	 * the last enum-const represents an entire type-tree rooted at some root
	 * type. For e.g. rooted at 'int', or 'struct abc'.
	 */
};

struct cc_symbol_table_entry {
	struct cc_node	*name;
	enum cc_symbol_table_entry_type	type;
	union {
		struct cc_type	*type;	/* For objects and funcs */
		struct cc_type	*root;	/* Only for _TYPE_TREE */
		struct cc_symbol_table_entry	*target;	/* type-def's target */
	} u;
};
static void cc_symbol_table_entry_delete(void *p);

static inline
enum cc_symbol_table_entry_type
cc_symbol_table_entry_type(const struct cc_symbol_table_entry *this)
{
	return this->type;
}

/*
 * A sym-tab implicitly denotes a scope, as each scope needs its own
 * sym-tab, even if empty.
 *
 * There is one file-scope (i.e. global symbol table) for the translation unit.
 * Each func-prototype has its own sym-tab that defines the func-prototype
 * scope.
 *
 * The function body will have two sym-tabs:
 *	- a block-level sym-tab for the func-body. This sym-tab points back to the
 *	  func-prototype sym-tab.
 *	- a func-level sym-tab for labels.
 *
 * All other identifiers have their scope determined by placement of their
 * declaration.
 */
struct cc_symbol_table {
	struct ptr_tree		tree;
	struct ptr_queue	entries;
};
static void cc_symbol_table_delete(void *p);

static inline
void cc_symbol_table_init(struct cc_symbol_table *this)
{
	ptrt_init(&this->tree, cc_symbol_table_delete);
	ptrq_init(&this->entries, cc_symbol_table_entry_delete);
}

static inline
err_t cc_symbol_table_add_entry(struct cc_symbol_table *this,
								struct cc_symbol_table_entry *entry)
{
	return ptrq_add_tail(&this->entries, entry);
}
/*****************************************************************************/
struct compiler {
	struct cc_node	*root;				/* root of the ast */
	struct cc_symbol_table	*symbols;	/* root of the sym-tab-tree */

	int	cpp_tokens_fd;
	const char	*cpp_tokens_path;
	struct cc_token_stream	stream;
};
#endif
