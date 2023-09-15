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
struct cc_symtab_entry;
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
	 * For string-literals and char-consts, this is in exec-char-set.
	 * For numbers, it is in src-char-set.
	 * For keywords, it is null.
	 * For identifiers, it is cpp's resolved name. i.e. any esc-seqs in the
	 * original src were resolved to the corresponding src-char-set-encoded
	 * byte stream (src-char-set is assume to be utf-8.) That stream is stored
	 * here.
	 */
	const char	*string;
	size_t		string_len;	/* len doesn't include the terminating nul */

	/*
	 * For e.g. a node of type CC_NODE_PLUS (binary +) will report the
	 * type of addition here (int, char, float, bit-int, etc).
	 * For statements and other constructs, the type is void, represented
	 * by a out_type == NULL.
	 */
	struct cc_symtab_entry	*out_type;
};

static void cc_node_delete(void *p);

static inline
void cc_node_init(struct cc_node *this)
{
	this->type = CC_TOKEN_INVALID;
	this->out_type = NULL;
	this->string = NULL;
	this->string_len = 0;
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
/*
 * sym-tab stores information about each identifier declared.
 * The declaration of each identifier determines its scope. The constructs
 * surrounding the identifier determine its name-space.
 */
enum cc_name_space_type {
	CC_NAME_SPACE_LABELS,
	CC_NAME_SPACE_STRUCT_TAGS,
	CC_NAME_SPACE_UNION_TAGS,
	CC_NAME_SPACE_ENUM_TAGS,
	CC_NAME_SPACE_MEMBERS,
	CC_NAME_SPACE_ORDINARY,
	CC_NAME_SPACE_STD_ATTRS,
	CC_NAME_SPACE_ATTR_PREFIXES,
	CC_NAME_SPACE_ATTR_PREFIXED_ATTRS,
	CC_NAME_SPACE_MAX
};

enum cc_linkage_type {
	CC_LINKAGE_NONE,
	CC_LINKAGE_EXTERNAL,
	CC_LINKAGE_INTERNAL,
};

enum cc_symtab_entry_type {
	CC_SYMTAB_ENTRY_INVALID,

	CC_SYMTAB_ENTRY_TYPE_DEF,

	/* Various types */
#if 0
	CC_SYMTAB_ENTRY_FLOAT,
	CC_SYMTAB_ENTRY_DOUBLE,
	CC_SYMTAB_ENTRY_LONG_DOUBLE,
	CC_SYMTAB_ENTRY_DECIMAL_32,
	CC_SYMTAB_ENTRY_DECIMAL_64,
	CC_SYMTAB_ENTRY_DECIMAL_128,
	CC_SYMTAB_ENTRY_COMPLEX,
#endif
	CC_SYMTAB_ENTRY_INTEGER,		/* int types */
	CC_SYMTAB_ENTRY_BIT_INT,
	CC_SYMTAB_ENTRY_BIT_FIELD,
	CC_SYMTAB_ENTRY_SIGNED,
	CC_SYMTAB_ENTRY_UNSIGNED,

	CC_SYMTAB_ENTRY_ENUMERATION,	/* other types */
	CC_SYMTAB_ENTRY_STRUCTURE,
	CC_SYMTAB_ENTRY_UNION,
	CC_SYMTAB_ENTRY_ARRAY,
	CC_SYMTAB_ENTRY_POINTER,
	CC_SYMTAB_ENTRY_FUNCTION,
	CC_SYMTAB_ENTRY_VOID,

	/* qualifiers to qualify type take the same form as others */
	CC_SYMTAB_ENTRY_CONST,
	CC_SYMTAB_ENTRY_RESTRICT,
	CC_SYMTAB_ENTRY_VOLATILE,
	CC_SYMTAB_ENTRY_ATOMIC,

	CC_SYMTAB_ENTRY_OBJECT,
};

struct cc_symtab_entry_typedef {
	/* The entry of the target type of this typedef */
	struct cc_symtab_entry	*type;
};

/*
 * We assume that sizeof(int) >= 4 bytes = 32 bits.
 * alignments are represented as values of type size_t. But we use int here as
 * alignof(max_align_t) is 0x10, which is representable in an int.
 * When an ast-node for alignof(int), for e.g., is build, the node's out_type
 * is set to size_t, as alignof() returns its value in size_t type.
 */
struct cc_symtab_entry_integer {
	int		width;		/* in bits. padding + value + sign */
	int		precision;	/* in bits. value bits */
	int		padding;	/* in bits. */
	int		alignment;	/* in bits. */
	bool	is_signed;
};

struct cc_symtab_entry_bit_field {
	struct cc_symtab_entry	*type;	/* Underlying type: int/bool */
	int width;
	int	offset;
};

struct cc_symtab_entry_pointer {
	struct cc_symtab_entry	*type;	/* The referenced type */
};

struct cc_symtab_entry_array {
	struct cc_symtab_entry	*type;	/* The element type */
	int		num_elements;	/* TODO: type of index? */
	bool	is_vla;
};

/* Used for both struct/union. Enum has the same symtab as entry.parent */
struct cc_symtab_entry_struct {
	struct cc_symtab	*symbols;
};

/* Used for both func-decl and func-defn */
struct cc_symtab_entry_function {
	struct cc_symtab_entry	*prev;	/* Previous declaration, if any */
	struct cc_symtab_entry	*type;	/* The return type */
	struct cc_symtab		*symbols;	/* Parameters. Names may be null */
};

struct cc_symtab_entry_object {
	struct cc_symtab_entry	*type;
};

struct cc_symtab_entry {
	struct cc_symtab	*parent;	/* The table containing this entry */
	struct cc_node		*symbol;	/* an identifier */
	enum cc_symtab_entry_type	type;
	enum cc_linkage_type	linkage;
	enum cc_token_type		storage;
	enum cc_name_space_type	name_space;
	union {
		struct cc_symtab_entry_typedef		type_def;
		struct cc_symtab_entry_integer		integer;
		struct cc_symtab_entry_bit_field	bit_field;
		struct cc_symtab_entry_pointer		pointer;
		struct cc_symtab_entry_array		array;
		struct cc_symtab_entry_struct		struct_union;
		struct cc_symtab_entry_function		function;
		struct cc_symtab_entry_object		object;
	} u;
};
static void cc_symtab_entry_delete(void *p);

static inline
enum cc_symtab_entry_type
cc_symtab_entry_type(const struct cc_symtab_entry *this)
{
	return this->type;
}

/*
 * The entries are divided into 6 name-spaces. Not all name-spaces are
 * valid for a given scope. For e.g., for file or global
 * scope, the members and labels queue must be empty.
 */
struct cc_symtab {
	struct ptr_tree		tree;	/* parent == NULL => file/global scope */
	struct ptr_queue	entries[CC_NAME_SPACE_MAX];
};
static void cc_symtab_delete(void *p);

static inline
void cc_symtab_init(struct cc_symtab *this)
{
	int i;
	ptrt_init(&this->tree, cc_symtab_delete);
	for (i = 0; i < CC_NAME_SPACE_MAX; ++i)
		ptrq_init(&this->entries[i], cc_symtab_entry_delete);
}

static inline
err_t cc_symtab_add_entry(struct cc_symtab *this,
						  struct cc_symtab_entry *entry)
{
	enum cc_name_space_type ns = entry->name_space;
	return ptrq_add_tail(&this->entries[ns], entry);
}
/*****************************************************************************/
struct compiler {
	struct cc_node		*root;		/* root of the ast */
	struct cc_symtab	*symbols;	/* root of the sym-tab-tree */

	int	cpp_tokens_fd;
	const char	*cpp_tokens_path;
	struct cc_token_stream	stream;
};
#endif
