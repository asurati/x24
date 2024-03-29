/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef SRC_CC_PARSER_H
#define SRC_CC_PARSER_H

#include <inc/cc/parser.h>

#include "token.h"

#include <inc/bits.h>
#include <inc/types.h>
/*****************************************************************************/
/* Only std attributes */
#define CC_ATTRIBUTE_DEPRECATED_POS		0
#define CC_ATTRIBUTE_FALL_THROUGH_POS	1
#define CC_ATTRIBUTE_NO_DISCARD_POS		2
#define CC_ATTRIBUTE_MAY_BE_UNUSED_POS	3
#define CC_ATTRIBUTE_NO_RETURN_POS		4
#define CC_ATTRIBUTE_UNSEQUENCED_POS	5
#define CC_ATTRIBUTE_REPRODUCIBLE_POS	6
#define CC_ATTRIBUTE_DEPRECATED_BITS	1
#define CC_ATTRIBUTE_FALL_THROUGH_BITS	1
#define CC_ATTRIBUTE_NO_DISCARD_BITS	1
#define CC_ATTRIBUTE_MAY_BE_UNUSED_BITS	1
#define CC_ATTRIBUTE_NO_RETURN_BITS		1
#define CC_ATTRIBUTE_UNSEQUENCED_BITS	1
#define CC_ATTRIBUTE_REPRODUCIBLE_BITS	1

#define CC_STORAGE_SPECIFIER_AUTO_POS			0
#define CC_STORAGE_SPECIFIER_CONST_EXPR_POS		1
#define CC_STORAGE_SPECIFIER_EXTERN_POS			2
#define CC_STORAGE_SPECIFIER_REGISTER_POS		3
#define CC_STORAGE_SPECIFIER_STATIC_POS			4
#define CC_STORAGE_SPECIFIER_THREAD_LOCAL_POS	5
#define CC_STORAGE_SPECIFIER_TYPE_DEF_POS		6
#define CC_STORAGE_SPECIFIER_AUTO_BITS			1
#define CC_STORAGE_SPECIFIER_CONST_EXPR_BITS	1
#define CC_STORAGE_SPECIFIER_EXTERN_BITS		1
#define CC_STORAGE_SPECIFIER_REGISTER_BITS		1
#define CC_STORAGE_SPECIFIER_STATIC_BITS		1
#define CC_STORAGE_SPECIFIER_THREAD_LOCAL_BITS	1
#define CC_STORAGE_SPECIFIER_TYPE_DEF_BITS		1

#define CC_FUNCTION_SPECIFIER_INLINE_POS		0
#define CC_FUNCTION_SPECIFIER_NO_RETURN_POS		1
#define CC_FUNCTION_SPECIFIER_INLINE_BITS		1
#define CC_FUNCTION_SPECIFIER_NO_RETURN_BITS	1

#define CC_TYPE_QUALIFIER_CONST_POS		0
#define CC_TYPE_QUALIFIER_RESTRICT_POS	1
#define CC_TYPE_QUALIFIER_VOLATILE_POS	2
#define CC_TYPE_QUALIFIER_ATOMIC_POS	3
#define CC_TYPE_QUALIFIER_CONST_BITS	1
#define CC_TYPE_QUALIFIER_RESTRICT_BITS	1
#define CC_TYPE_QUALIFIER_VOLATILE_BITS	1
#define CC_TYPE_QUALIFIER_ATOMIC_BITS	1

#define CC_TYPE_SPECIFIER_VOID_POS				0
#define CC_TYPE_SPECIFIER_CHAR_POS				1
#define CC_TYPE_SPECIFIER_SHORT_POS				2
#define CC_TYPE_SPECIFIER_INT_POS				3
#define CC_TYPE_SPECIFIER_LONG_0_POS			4
#define CC_TYPE_SPECIFIER_LONG_1_POS			5
#define CC_TYPE_SPECIFIER_FLOAT_POS				6
#define CC_TYPE_SPECIFIER_DOUBLE_POS			7
#define CC_TYPE_SPECIFIER_SIGNED_POS			8
#define CC_TYPE_SPECIFIER_UNSIGNED_POS			9
#define CC_TYPE_SPECIFIER_BIT_INT_POS			10
#define CC_TYPE_SPECIFIER_BOOL_POS				11
#define CC_TYPE_SPECIFIER_COMPLEX_POS			12
#define CC_TYPE_SPECIFIER_DECIMAL_32_POS		13
#define CC_TYPE_SPECIFIER_DECIMAL_64_POS		14
#define CC_TYPE_SPECIFIER_DECIMAL_128_POS		15
#define CC_TYPE_SPECIFIER_ATOMIC_POS			16
#define CC_TYPE_SPECIFIER_STRUCT_POS			17
#define CC_TYPE_SPECIFIER_UNION_POS				18
#define CC_TYPE_SPECIFIER_ENUM_POS				19
#define CC_TYPE_SPECIFIER_TYPE_DEF_NAME_POS		20
#define CC_TYPE_SPECIFIER_TYPE_OF_POS			21
#define CC_TYPE_SPECIFIER_TYPE_OF_UNQUAL_POS	22
#define CC_TYPE_SPECIFIER_VOID_BITS				1
#define CC_TYPE_SPECIFIER_CHAR_BITS				1
#define CC_TYPE_SPECIFIER_SHORT_BITS			1
#define CC_TYPE_SPECIFIER_INT_BITS				1
#define CC_TYPE_SPECIFIER_LONG_0_BITS			1
#define CC_TYPE_SPECIFIER_LONG_1_BITS			1
#define CC_TYPE_SPECIFIER_FLOAT_BITS			1
#define CC_TYPE_SPECIFIER_DOUBLE_BITS			1
#define CC_TYPE_SPECIFIER_SIGNED_BITS			1
#define CC_TYPE_SPECIFIER_UNSIGNED_BITS			1
#define CC_TYPE_SPECIFIER_BIT_INT_BITS			1
#define CC_TYPE_SPECIFIER_BOOL_BITS				1
#define CC_TYPE_SPECIFIER_COMPLEX_BITS			1
#define CC_TYPE_SPECIFIER_DECIMAL_32_BITS		1
#define CC_TYPE_SPECIFIER_DECIMAL_64_BITS		1
#define CC_TYPE_SPECIFIER_DECIMAL_128_BITS		1
#define CC_TYPE_SPECIFIER_ATOMIC_BITS			1
#define CC_TYPE_SPECIFIER_STRUCT_BITS			1
#define CC_TYPE_SPECIFIER_UNION_BITS			1
#define CC_TYPE_SPECIFIER_ENUM_BITS				1
#define CC_TYPE_SPECIFIER_TYPE_DEF_NAME_BITS	1
#define CC_TYPE_SPECIFIER_TYPE_OF_BITS			1
#define CC_TYPE_SPECIFIER_TYPE_OF_UNQUAL_BITS	1
/*****************************************************************************/
struct cc_node;
enum cc_type_type {
	CC_TYPE_INVALID,

	CC_TYPE_VOID,
	CC_TYPE_BOOL,

	CC_TYPE_CHAR,
	CC_TYPE_SHORT,
	CC_TYPE_INT,
	CC_TYPE_LONG,
	CC_TYPE_LONG_LONG,

	CC_TYPE_BIT_FIELD,
	CC_TYPE_BIT_INT,

	CC_TYPE_SIGNED,
	CC_TYPE_UNSIGNED,

	CC_TYPE_ATOMIC,
	CC_TYPE_POINTER,
	CC_TYPE_ARRAY,
	CC_TYPE_STRUCT,
	CC_TYPE_UNION,
	CC_TYPE_FUNCTION,
	CC_TYPE_ENUM,
};

/*
 * We assume that sizeof(int) >= 4 bytes = 32 bits.
 * alignments are represented as values of type size_t. But we use int here as
 * alignof(max_align_t) is 0x10, which is representable in an int.
 * When an ast-node for alignof(int), for e.g., is build, the node's out_type
 * is set to size_t, as alignof() returns its value in size_t type.
 * If the type is bool, it is always unsigned.
 * Any other integer-type is always signed.
 */
struct cc_type_integer {
	int		width;		/* in bits. padding + value + sign */
	int		precision;	/* in bits. value bits */
	int		padding;	/* in bits. */
	int		alignment;	/* in bits. */
};

struct cc_type_bit_field {
	int width;
	int	offset;	/* offset from start of the rep. of the underlying type */
};

/*
 * if the array is a func-param, the type-qualifiers if any are processed,
 * the type created, and the array is made to point to that type.
 */
struct cc_type_array {
	size_t	num_elements;
	struct cc_node	*expr;
	bool	has_static;
	bool	is_vla;
};

/*
 * Used for both struct/union. Enum has the same symtab as entry.parent.
 * The alignment of a struct can be recursively found using the alignment
 * of its first member.
 *
 * The members of an anonymous structure or union are members of the containing
 * structure or union, keeping their structure or union layout.
 */
struct cc_type_struct {
	struct cc_node	*symbols;
};

/*
 * no need for a separate cc_type_pointer structure.
 * CC_TYPE_POINTER and the hierarchy is enough.
 */

struct cc_type_function {
	struct cc_node	*block;
};

/* These entries are stored in scope-sym-tab[enum_tags_ns] */
struct cc_type_enum {
	bool	is_fixed;	/* is the underlying type fixed */
};

struct cc_type {
	struct ptr_tree		tree;
	enum cc_type_type	type;
	struct cc_node		*symbol;	/* points back to type's name */
	union {
		struct cc_type_integer		*integer;
		struct cc_type_bit_field	*bit_field;
		struct cc_type_array		*array;
		struct cc_type_struct		*struct_union;
		struct cc_type_function		*function;
		struct cc_type_enum			*enumeration;
	} u;
};

#define CC_TYPE_FOR_EACH_CHILD(p, ix, c)	\
	PTRT_FOR_EACH_CHILD(&(p)->tree, ix, c)

static inline
enum cc_type_type cc_type_type(const struct cc_type *this)
{
	return this->type;
}

static inline
int cc_type_num_children(const struct cc_type *this)
{
	return ptrt_num_children(&this->tree);
}

static inline
struct cc_type *cc_type_peek_child(const struct cc_type *this,
								   const int index)
{
	return ptrt_peek_child(&this->tree, index);
}

static inline
struct cc_type *cc_type_parent(struct cc_type *this)
{
	return ptrt_parent(&this->tree);
}
/*****************************************************************************/
enum cc_node_type {
#define DEF(t)	CC_NODE_ ## t,
#define NODE(t) CC_NODE_ ## t,
#include <inc/cpp/tokens.h>	/* grammar terminals */
#include <inc/cc/tokens.h>	/* grammar non-terminals */
#undef DEF
#undef NODE
};
#if 0
static inline
bool cc_node_type_is_terminal(const enum cc_node_type this)
{
	assert(this != CC_NODE_NUMBER && this != CC_NODE_INVALID);
	return this > CC_NODE_INVALID && this <= CC_NODE_WHILE;
}

static inline
bool cc_node_type_is_non_terminal(const enum cc_node_type this)
{
	return !cc_node_type_is_terminal(this);
}
#endif
static inline
bool cc_node_type_is_key_word(const enum cc_node_type this)
{
	return this >= CC_NODE_ATOMIC && this <= CC_NODE_WHILE;
}

static inline
bool cc_node_type_is_punctuator(const enum cc_node_type this)
{
	return this >= CC_NODE_LEFT_BRACE && this <= CC_NODE_ELLIPSIS;
}

static inline
bool cc_node_type_is_identifier(const enum cc_node_type this)
{
	return this == CC_NODE_IDENTIFIER;
}

static inline
bool cc_node_type_is_string_literal(const enum cc_node_type this)
{
	return (this >= CC_NODE_CHAR_STRING_LITERAL &&
			this <= CC_NODE_WCHAR_T_STRING_LITERAL);
}

static inline
bool cc_node_type_is_char_const(const enum cc_node_type this)
{
	return (this >= CC_NODE_INTEGER_CHAR_CONST &&
			this <= CC_NODE_WCHAR_T_CHAR_CONST);
}

static inline
bool cc_node_type_is_number(const enum cc_node_type this)
{
	return this >= CC_NODE_INTEGER_CONST && this <= CC_NODE_FLOATING_CONST;
}

static inline
bool cc_node_type_is_symbol(const enum cc_node_type this)
{
	return this >= CC_NODE_SYMBOL && this <= CC_NODE_SYMBOL_TYPE_DEF;
}

static inline
bool cc_node_type_is_storage_specifier(const enum cc_node_type this)
{
	return (this == CC_NODE_AUTO ||
			this == CC_NODE_CONST_EXPR ||
			this == CC_NODE_EXTERN ||
			this == CC_NODE_REGISTER ||
			this == CC_NODE_STATIC ||
			this == CC_NODE_THREAD_LOCAL ||
			this == CC_NODE_TYPE_DEF);
}

static inline
bool cc_node_type_is_type_specifier(const enum cc_node_type this)
{
	return (this == CC_NODE_VOID ||
			this == CC_NODE_CHAR ||
			this == CC_NODE_SHORT ||
			this == CC_NODE_INT ||
			this == CC_NODE_LONG ||
			this == CC_NODE_FLOAT ||
			this == CC_NODE_DOUBLE ||
			this == CC_NODE_SIGNED ||
			this == CC_NODE_UNSIGNED ||
			this == CC_NODE_BIT_INT ||
			this == CC_NODE_BOOL ||
			this == CC_NODE_COMPLEX ||
			this == CC_NODE_DECIMAL_32 ||
			this == CC_NODE_DECIMAL_64 ||
			this == CC_NODE_DECIMAL_128 ||
			this == CC_NODE_ATOMIC ||
			this == CC_NODE_STRUCT ||
			this == CC_NODE_UNION ||
			this == CC_NODE_ENUM ||
			this == CC_NODE_TYPE_OF ||
			this == CC_NODE_TYPE_OF_UNQUAL ||
			this == CC_NODE_IDENTIFIER);
}

static inline
bool cc_node_type_is_type_qualifier(const enum cc_node_type this)
{
	return (this == CC_NODE_CONST ||
			this == CC_NODE_RESTRICT ||
			this == CC_NODE_VOLATILE ||
			this == CC_NODE_ATOMIC);
}

static inline
bool cc_node_type_is_alignment_specifier(const enum cc_node_type this)
{
	return this == CC_NODE_ALIGN_AS;
}

static inline
bool cc_node_type_is_function_specifier(const enum cc_node_type this)
{
	return this == CC_NODE_INLINE || this == CC_NODE_NO_RETURN;
}
/*****************************************************************************/
struct cc_node_type_specifiers {
	struct cc_type	*type;
	int	mask;
};

struct cc_node_type_qualifiers {
	int	mask;
};

struct cc_node_storage_specifiers {
	int	mask;
};

struct cc_node_function_specifiers {
	int	mask;
};

struct cc_node_attribute_specifiers {
	int	mask;
};

struct cc_node_alignment_specifiers {
	int	alignment;
};

/* With each DeclarationSpecifier, update the type */
struct cc_node_declaration_specifiers {
	struct cc_type	*type;	/* The resultant type */
	struct cc_node	*type_specifiers;
	struct cc_node	*type_qualifiers;		/* applies to type */
	struct cc_node	*attribute_specifiers;	/* applies to type */
	struct cc_node	*alignment_specifiers;	/* applies to type */
	struct cc_node	*storage_specifiers;	/* applies to ident */
	struct cc_node	*function_specifiers;	/* applies to ident */
};

struct cc_node_declarator {
	struct ptr_queue	list;	/* type-list cc_node * */
};

/*
 * For string-literals and char-consts, this is in exec-char-set.
 * For numbers, it is in src-char-set.
 * For keywords, it is null.
 * For identifiers, it is cpp's resolved name. i.e. any esc-seqs in the
 * original src were resolved to the corresponding src-char-set-encoded
 * byte stream (src-char-set is assume to be utf-8.) That stream is stored
 * here.
 */
struct cc_node_number {
	const char	*string;
	size_t		string_len;	/* doesn't include the terminated nul */
};

struct cc_node_char_const {
	const char	*string;
	size_t		string_len;	/* doesn't include the terminated nul */
};

struct cc_node_string_literal {
	const char	*string;
	size_t		string_len;	/* doesn't include the terminated nul */
};

struct cc_node_identifier {
	const char	*string;
	size_t		string_len;	/* doesn't include the terminated nul */
	struct cc_node	*symbol;
	struct cc_node	*function_specifiers;
	struct cc_node	*attributes;
};

/*
 * always unnamed.
 * Goes into ordinary name-space, where the func-name also resides.
 * For func-decl, symbols contains the parameters.
 * For func-defn, it contains the parameters + labels + any function-level
 * vars + inner blocks.
 *
 * symtab_entry.prev links the blocks in parent-child relation.
 * block isn't really a type, like a function is.
 *
 * code of a block is the stmt-list, stored as its children.
 */
struct cc_node_block {
	struct cc_node	*symbols;
};

/* These entries are stored in scope-sym-tab[ordinary_ns] */
struct cc_node_object {
	struct cc_node	*type;
	/* For enum-constants, type points back to the enum-type */
};

/*
 * sym-tab stores information about each identifier declared.
 * The declaration of each identifier determines its scope. The constructs
 * surrounding the identifier determine its name-space.
 */
enum cc_name_space_type {
	CC_NAME_SPACE_LABEL,
	CC_NAME_SPACE_TAG,		/* struct/union/enum */
	CC_NAME_SPACE_MEMBER,	/* struct/union */
	CC_NAME_SPACE_ORDINARY,
	CC_NAME_SPACE_ATTRIBUTE,	/* std-attr + attr-prefix */
	CC_NAME_SPACE_PREFIXED_ATTRIBUTE,
	CC_NAME_SPACE_MAX,
};

enum cc_scope {
	CC_SCOPE_FILE,
	CC_SCOPE_BLOCK,
	CC_SCOPE_PROTOTYPE,
	CC_SCOPE_MEMBER,	/* Only *_MEMBER name-space is filled */
};

struct cc_node_symbols {
	struct ptr_queue	entries[CC_NAME_SPACE_MAX];
	enum cc_scope		scope;
};
static inline
enum cc_scope cc_node_symbols_scope(const struct cc_node_symbols *this)
{
	return this->scope;
}

#define CC_LINKAGE_NONE		CC_NODE_INVALID
#define CC_LINKAGE_STATIC	CC_NODE_STATIC
#define CC_LINKAGE_EXTERN	CC_NODE_EXTERN

#define CC_STORAGE_NONE			CC_NODE_INVALID
#define CC_STORAGE_THREAD_LOCAL	CC_NODE_THREAD_LOCAL
#define CC_STORAGE_STATIC		CC_NODE_STATIC

/*
 * A symbol is a name-value pair.
 * For a type, type == value. This is how types are recognized.
 * For identifiers (excluding type-defs) that are types, type and value point
 * to the type cc_node.
 * For type-defs and identifiers that are not types, type points to the
 * symtab-entry that contains the symbol for the type.
 * the root of a type-tree is housed in SYMBOL_TYPE.type.
 */
struct cc_node_symbol {
	struct cc_node	*symbols;		/* The table containing this entry */
	struct cc_node	*prev;			/* used to link same-name declarations */
	struct cc_node	*identifier;	/* an identifier, or null */
	struct cc_type	*type;			/* must not be null */
	struct cc_node	*init;
	enum cc_node_type	linkage;
	enum cc_node_type	storage;
	enum cc_name_space_type	name_space;
};

/* Not all cc_node_types need a member in the union */
struct cc_node {
	struct ptr_tree		tree;	/* rooted at this node */
	enum cc_node_type	type;
	union {
		struct cc_node_object	*object;
		struct cc_node_number	*number;
		struct cc_node_char_const		*char_const;
		struct cc_node_string_literal	*string_literal;
		struct cc_node_identifier		*identifier;

		/*
		 * These are collectors of information during parsing. They are not
		 * found in symtabs. Attributes are found in the type-trees when they
		 * modify a type, or in Identifiers whney they modify idents.
		 */
		struct cc_node_attribute_specifiers		*attribute_specifiers;
		struct cc_node_type_specifiers          *type_specifiers;
		struct cc_node_type_qualifiers          *type_qualifiers;
		struct cc_node_function_specifiers      *function_specifiers;
		struct cc_node_storage_specifiers       *storage_specifiers;
		struct cc_node_alignment_specifiers     *alignment_specifiers;
		struct cc_node_declaration_specifiers	*declaration_specifiers;
		struct cc_node_declarator				*declarator;
		/* Declarator also functions as AbstractDeclarator */

		struct cc_node_symbols			*symbols;
		struct cc_node_symbol			*symbol;

		struct cc_node_block			*block;
	} u;
};

#define CC_NODE_FOR_EACH_CHILD(p, ix, c)	\
	PTRT_FOR_EACH_CHILD(&(p)->tree, ix, c)

static inline
enum cc_node_type cc_node_type(const struct cc_node *this)
{
	return this->type;
}

static inline
int cc_node_num_children(const struct cc_node *this)
{
	return ptrt_num_children(&this->tree);
}

static inline
struct cc_node *cc_node_peek_child(const struct cc_node *this,
								   const int index)
{
	return ptrt_peek_child(&this->tree, index);
}

static inline
struct cc_node *cc_node_parent(struct cc_node *this)
{
	return ptrt_parent(&this->tree);
}

static inline
bool cc_node_is_identifier(const struct cc_node *this)
{
	return cc_node_type_is_identifier(cc_node_type(this));
}

static inline
bool cc_node_is_key_word(const struct cc_node *this)
{
	return cc_node_type_is_key_word(cc_node_type(this));
}

static inline
bool cc_node_is_number(const struct cc_node *this)
{
	return cc_node_type_is_number(cc_node_type(this));
}

static inline
bool cc_node_is_char_const(const struct cc_node *this)
{
	return cc_node_type_is_char_const(cc_node_type(this));
}

static inline
bool cc_node_is_string_literal(const struct cc_node *this)
{
	return cc_node_type_is_string_literal(cc_node_type(this));
}

static inline
bool cc_node_is_symbol(const struct cc_node *this)
{
	return cc_node_type_is_symbol(cc_node_type(this));
}

static inline
err_t cc_node_add_tail_child(struct cc_node *this,
							 struct cc_node *child)
{
	return ptrt_add_tail_child(&this->tree, child);
}
/*****************************************************************************/
struct parser {
	struct cc_node	*root;		/* root of the ast */
	struct cc_node	*symbols;	/* current sym-table */

	int	cpp_tokens_fd;
	const char	*cpp_tokens_path;
	struct cc_token_stream	stream;
};

static inline
struct cc_token_stream *parser_token_stream(struct parser *this)
{
	return &this->stream;
}
#endif
