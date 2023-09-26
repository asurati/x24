/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include "parser.h"

#include <inc/unicode.h>

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
static
err_t parser_parse_declarator(struct parser *,
							  struct cc_node **);
/*****************************************************************************/
/* From src/cpp/lexer.c */
extern const char *g_key_words[];
extern const char *g_punctuators[];
static const char *g_cc_token_type_str[] = {
#define DEF(t)	"CC_TOKEN_" # t,
#define NODE(t)
#include <inc/cpp/tokens.h>
#include <inc/cc/tokens.h>
#undef DEF
#undef NODE
};

static const char *g_cc_node_type_str[] = {
#define DEF(t)	"CC_NODE_" # t,
#define NODE(t)	"CC_NODE_" # t,
#include <inc/cpp/tokens.h>
#include <inc/cc/tokens.h>
#undef DEF
#undef NODE
};
/*****************************************************************************/
void cc_token_delete(void *p)
{
	struct cc_token *this = p;
	/*
	 * Do not free the string for punctuator & key-words. They are
	 * allocated in .rodata.
	 */
	if (!cc_token_is_key_word(this) && !cc_token_is_punctuator(this))
		free((void *)this->string);
	free(this);
}

//static
void cc_token_print(const struct cc_token *this)
{
	const char *type = g_cc_token_type_str[this->type];
	printf("%s: %s", __func__, type);
	if (this->string)
		printf(" '%s'", this->string);
	printf("\n");
}

//static
bool cc_token_is_standard_attribute(const struct cc_token *this,
									const enum cc_token_type type)
{
	const char *strings[2];

	assert(type >= CC_TOKEN_NO_RETURN && type <= CC_TOKEN_REPRODUCIBLE);
	if (!cc_token_is_identifier(this))
		return false;
	strings[0] = cc_token_string(this);
	strings[1] = g_key_words[type - CC_TOKEN_ATOMIC];
	return strcmp(strings[0], strings[1]) == 0;
}
/*****************************************************************************/
static
void cc_node_delete(void *p)
{
	/* TODO free based on type */
	struct cc_node *this = p;
	ptrt_empty(&this->tree);
	free(this);
}

static
struct cc_node *cc_node_new(const enum cc_node_type type)
{
	struct cc_node *this = calloc(1, sizeof(*this));
	if (this == NULL)
		return NULL;
	this->type = type;
	ptrt_init(&this->tree, cc_node_delete);
	return this;
}

static
struct cc_node *cc_node_new_symbols(const enum cc_scope scope)
{
	int i;
	struct cc_node *this;
	struct cc_node_symbols *ss;

	this = cc_node_new(CC_NODE_SYMBOLS);
	ss = calloc(1, sizeof(*ss));
	if (this == NULL || ss == NULL)
		return NULL;
	this->u.symbols = ss;
	ss->scope = scope;
	for (i = 0; i < CC_NAME_SPACE_MAX; ++i)
		ptrq_init(&ss->entries[i], cc_node_delete);
	return this;
}

static
struct cc_node *cc_node_new_symbol(struct cc_node *symbols,
								   const enum cc_node_type type)
{
	struct cc_node *this;
	struct cc_node_symbol *s;

	assert(cc_node_type_is_symbol(type));
	this = cc_node_new(type);
	s = calloc(1, sizeof(*s));
	if (this == NULL || s == NULL)
		return NULL;
	this->u.symbol = s;
	s->symbols = symbols;
	return this;
}

static
struct cc_node *cc_node_new_type_specifiers(void)
{
	struct cc_node *this;
	struct cc_node_type_specifiers *ts;

	this = cc_node_new(CC_NODE_TYPE_SPECIFIERS);
	ts = calloc(1, sizeof(*ts));
	if (this == NULL || ts == NULL)
		return NULL;
	this->u.type_specifiers = ts;
	return this;
}

static
struct cc_node *cc_node_new_type_qualifiers(void)
{
	struct cc_node *this;
	struct cc_node_type_qualifiers *tq;

	this = cc_node_new(CC_NODE_TYPE_QUALIFIERS);
	tq = calloc(1, sizeof(*tq));
	if (this == NULL || tq == NULL)
		return NULL;
	this->u.type_qualifiers = tq;
	return this;
}

//static
struct cc_node *cc_node_new_function_specifiers(void)
{
	struct cc_node *this;
	struct cc_node_function_specifiers *fs;

	this = cc_node_new(CC_NODE_FUNCTION_SPECIFIERS);
	fs = calloc(1, sizeof(*fs));
	if (this == NULL || fs == NULL)
		return NULL;
	this->u.function_specifiers = fs;
	return this;
}

static
struct cc_node *cc_node_new_storage_specifiers(void)
{
	struct cc_node *this;
	struct cc_node_storage_specifiers *ss;

	this = cc_node_new(CC_NODE_STORAGE_SPECIFIERS);
	ss = calloc(1, sizeof(*ss));
	if (this == NULL || ss == NULL)
		return NULL;
	this->u.storage_specifiers = ss;
	return this;
}

//static
struct cc_node *cc_node_new_attributes(void)
{
	struct cc_node *this;
	struct cc_node_attributes *a;

	this = cc_node_new(CC_NODE_ATTRIBUTES);
	a = calloc(1, sizeof(*a));
	if (this == NULL || a == NULL)
		return NULL;
	this->u.attributes = a;
	return this;
}

/* Scope is block or prototype */
static
struct cc_node *cc_node_new_block(const enum cc_scope scope)
{
	struct cc_node *this, *symbols;
	struct cc_node_block *b;

	this = cc_node_new(CC_NODE_BLOCK);
	symbols = cc_node_new_symbols(scope);
	b = calloc(1, sizeof(*b));
	if (this == NULL || b == NULL || symbols == NULL)
		return NULL;
	b->symbols = symbols;
	this->u.block = b;
	return this;
}

//static
struct cc_node *cc_node_new_type_array(void)
{
	struct cc_node *this;
	struct cc_node_type_array *ta;

	this = cc_node_new(CC_NODE_TYPE_ARRAY);
	ta = calloc(1, sizeof(*ta));
	if (this == NULL || ta == NULL)
		return NULL;
	this->u.type_array = ta;
	return this;
}

static
struct cc_node *cc_node_new_type_function(void)
{
	struct cc_node *this, *block;
	struct cc_node_type_function *tf;

	/*
	 * scope is set to prototype initially. Once parser knows that it is
	 * parsing a func-defn, it changes the scope to block.
	 */
	block = cc_node_new_block(CC_SCOPE_PROTOTYPE);
	this = cc_node_new(CC_NODE_TYPE_FUNCTION);
	tf = calloc(1, sizeof(*tf));
	if (this == NULL || tf == NULL || block == NULL)
		return NULL;
	tf->block = block;
	this->u.type_function = tf;
	return this;
}

static
struct cc_node *cc_node_new_type_pointer(void)
{
	return cc_node_new(CC_NODE_TYPE_POINTER);
}

static
struct cc_node *cc_node_new_declarator(void)
{
	struct cc_node *this;
	struct cc_node_declarator *d;

	this = cc_node_new(CC_NODE_DECLARATOR);
	d = calloc(1, sizeof(*d));
	if (this == NULL || d == NULL)
		return NULL;
	ptrq_init(&d->list, cc_node_delete);
	this->u.declarator = d;
	return this;
}

static
struct cc_node *cc_node_new_declaration_specifiers(void)
{
	struct cc_node_declaration_specifiers *ds;
	struct cc_node *this;

	this = cc_node_new(CC_NODE_DECLARATION_SPECIFIERS);
	ds = calloc(1, sizeof(*ds));
	if (this == NULL || ds == NULL)
		return NULL;
	this->u.declaration_specifiers = ds;
	return this;
}

/* Claims ownership of string */
static
struct cc_node *cc_node_new_identifier(const char *string,
									   const size_t string_len)
{
	struct cc_node *this;
	struct cc_node_identifier *ident;

	assert(string);
	this = cc_node_new(CC_NODE_IDENTIFIER);
	ident = calloc(1, sizeof(*ident));
	if (this == NULL || ident == NULL)
		return NULL;
	ident->string = string;
	ident->string_len = string_len;
	this->u.identifier = ident;
	return this;
}

static
struct cc_node *cc_node_new_type_integer(const enum cc_node_type type)
{
	struct cc_node *this;
	struct cc_node_type_integer *ti;

	this = cc_node_new(type);
	ti = calloc(1, sizeof(*ti));
	if (this == NULL || ti == NULL)
		return NULL;
	this->u.type_integer = ti;
	return this;
}
/*****************************************************************************/
static inline
void *cc_node_assert_type(struct cc_node *this,
						  const enum cc_node_type type)
{
	void *ret = NULL;
	assert(this);
	assert(cc_node_type(this) == type);
	switch (type) {
	case CC_NODE_TRANSLATION_UNIT:
	case CC_NODE_DECLARATION_SPECIFIERS:
		return this;
	case CC_NODE_BLOCK:
		ret = this->u.block;
		break;
	case CC_NODE_TYPE_FUNCTION:
		ret = this->u.type_function;
		break;
	case CC_NODE_DECLARATOR:
		ret = this->u.declarator;
		break;
	case CC_NODE_TYPE_SPECIFIERS:
		ret = this->u.type_specifiers;
		break;
	case CC_NODE_STORAGE_SPECIFIERS:
		ret = this->u.storage_specifiers;
		break;
	case CC_NODE_SYMBOLS:
		ret = this->u.symbols;
		break;
	case CC_NODE_SYMBOL:
	case CC_NODE_SYMBOL_TYPE:
	case CC_NODE_SYMBOL_TYPE_DEF:
		ret = this->u.symbol;
		break;
	case CC_NODE_TYPE_BOOL:
	case CC_NODE_TYPE_CHAR:
	case CC_NODE_TYPE_SHORT:
	case CC_NODE_TYPE_INT:
	case CC_NODE_TYPE_LONG:
	case CC_NODE_TYPE_LONG_LONG:
		ret = this->u.type_integer;
		break;
	default:
		assert(0);
		return NULL;
	}
	assert(ret);
	return ret;
}
/*****************************************************************************/
static
err_t cc_node_storage_specifiers_add(struct cc_node_storage_specifiers *this,
									 const enum cc_token_type type)
{
	int mask = this->mask;

#if 0
	if (type == CC_TOKEN_THREAD_LOCAL) {
		/* Can't have more than one */
		if (bits_get(mask, CC_STORAGE_SPECIFIER_THREAD_LOCAL))
			return EINVAL;
		/* thread_local can be used with static/extern */
		mask &= bits_off(CC_STORAGE_SPECIFIER_STATIC);
		mask &= bits_off(CC_STORAGE_SPECIFIER_EXTERN);
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_STORAGE_SPECIFIER_THREAD_LOCAL);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_AUTO) {
		/* Can't have more than one */
		if (bits_get(mask, CC_STORAGE_SPECIFIER_AUTO))
			return EINVAL;
		/* auto can be used with all except typedef */
		if (bits_get(mask, CC_STORAGE_SPECIFIER_TYPE_DEF))
			return EINVAL;
		this->mask |= bits_on(CC_STORAGE_SPECIFIER_AUTO);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_CONST_EXPR) {
		/* Can't have more than one */
		if (bits_get(ts, CC_STORAGE_SPECIFIER_CONST_EXPR))
			return EINVAL;
		/* const-expr can be used with auto/register/static */
		ts &= bits_off(CC_STORAGE_SPECIFIER_AUTO);
		ts &= bits_off(CC_STORAGE_SPECIFIER_REGISTER);
		ts &= bits_off(CC_STORAGE_SPECIFIER_STATIC);
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_STORAGE_SPECIFIER_CONST_EXPR);
		return ESUCCESS;
	}
#endif
	if (type == CC_TOKEN_TYPE_DEF) {
		/* Can't have more than one */
		if (bits_get(mask, CC_STORAGE_SPECIFIER_TYPE_DEF))
			return EINVAL;
		/* type-def can't be used with anything else */
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_STORAGE_SPECIFIER_TYPE_DEF);
		return ESUCCESS;
	}
	assert(0);	/* TODO */
	return EINVAL;
}

static
err_t cc_node_add_storage_specifier(struct cc_node *this,
									const enum cc_token_type type)
{
	struct cc_node_storage_specifiers *ss;
	ss = cc_node_assert_type(this, CC_NODE_STORAGE_SPECIFIERS);
	return cc_node_storage_specifiers_add(ss, type);
}

static
bool cc_node_is_storage_specifier_type_def(struct cc_node *this)
{
	const struct cc_node_storage_specifiers *ss;
	ss = cc_node_assert_type(this, CC_NODE_STORAGE_SPECIFIERS);
	return bits_get(ss->mask, CC_STORAGE_SPECIFIER_TYPE_DEF);
}
/*****************************************************************************/
static
err_t cc_node_type_qualifiers_add(struct cc_node_type_qualifiers *this,
								  const enum cc_token_type type)
{
	if (type == CC_TOKEN_CONST)
		this->mask |= bits_on(CC_TYPE_QUALIFIER_CONST);
	else if (type == CC_TOKEN_RESTRICT)
		this->mask |= bits_on(CC_TYPE_QUALIFIER_RESTRICT);
	else if (type == CC_TOKEN_VOLATILE)
		this->mask |= bits_on(CC_TYPE_QUALIFIER_VOLATILE);
	else if (type == CC_TOKEN_ATOMIC)
		this->mask |= bits_on(CC_TYPE_QUALIFIER_ATOMIC);
	else
		return EINVAL;
	return ESUCCESS;
}

static
err_t cc_node_add_type_qualifier(struct cc_node *this,
								 const enum cc_token_type type)
{
	struct cc_node_type_qualifiers *tq;
	tq = cc_node_assert_type(this, CC_NODE_TYPE_QUALIFIERS);
	return cc_node_type_qualifiers_add(tq, type);
}
/*****************************************************************************/
static
err_t cc_node_type_specifiers_add(struct cc_node_type_specifiers *this,
								  const enum cc_token_type type)
{
	int mask = this->mask;

	if (type == CC_TOKEN_SIGNED ||
		type == CC_TOKEN_UNSIGNED) {
		/* Can't have the signedness more than once */
		if (bits_get(mask, CC_TYPE_SPECIFIER_SIGNED) ||
			bits_get(mask, CC_TYPE_SPECIFIER_UNSIGNED))
			return EINVAL;
		/* signed/unsigned can be used with char/short/int/long/bit-int */
		mask &= bits_off(CC_TYPE_SPECIFIER_CHAR);
		mask &= bits_off(CC_TYPE_SPECIFIER_SHORT);
		mask &= bits_off(CC_TYPE_SPECIFIER_INT);
		mask &= bits_off(CC_TYPE_SPECIFIER_LONG_0);
		mask &= bits_off(CC_TYPE_SPECIFIER_LONG_1);
		mask &= bits_off(CC_TYPE_SPECIFIER_BIT_INT);
		if (mask)
			return EINVAL;
		if (type == CC_TOKEN_SIGNED)
			this->mask |= bits_on(CC_TYPE_SPECIFIER_SIGNED);
		else
			this->mask |= bits_on(CC_TYPE_SPECIFIER_UNSIGNED);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_CHAR) {
		/* Can't have more than one chars */
		if (bits_get(mask, CC_TYPE_SPECIFIER_CHAR))
			return EINVAL;
		/* char can be used with signed/unsigned */
		mask &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		mask &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_TYPE_SPECIFIER_CHAR);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_SHORT) {
		/* Can't have more than one short */
		if (bits_get(mask, CC_TYPE_SPECIFIER_SHORT))
			return EINVAL;
		/* short can be used with int/signed/unsigned */
		mask &= bits_off(CC_TYPE_SPECIFIER_INT);
		mask &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		mask &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_TYPE_SPECIFIER_SHORT);
	}

	if (type == CC_TOKEN_INT) {
		/* Can't have more than one ints */
		if (bits_get(mask, CC_TYPE_SPECIFIER_INT))
			return EINVAL;
		/* int can be used with long/short/signed/unsigned */
		mask &= bits_off(CC_TYPE_SPECIFIER_LONG_0);
		mask &= bits_off(CC_TYPE_SPECIFIER_LONG_1);
		mask &= bits_off(CC_TYPE_SPECIFIER_SHORT);
		mask &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		mask &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_TYPE_SPECIFIER_INT);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_LONG) {
		/* Can't have more than 2 longs */
		if (bits_get(mask, CC_TYPE_SPECIFIER_LONG_0) &&
			bits_get(mask, CC_TYPE_SPECIFIER_LONG_1))
			return EINVAL;
		/* long can be used with int/long/signed/unsigned */
		mask &= bits_off(CC_TYPE_SPECIFIER_LONG_0);
		mask &= bits_off(CC_TYPE_SPECIFIER_LONG_1);
		mask &= bits_off(CC_TYPE_SPECIFIER_INT);
		mask &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		mask &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (mask)
			return EINVAL;
		mask = this->mask;
		if (!bits_get(mask, CC_TYPE_SPECIFIER_LONG_0))
			this->mask |= bits_on(CC_TYPE_SPECIFIER_LONG_0);
		else
			this->mask |= bits_on(CC_TYPE_SPECIFIER_LONG_1);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_BIT_INT) {
		/* Can't have more than one */
		if (bits_get(mask, CC_TYPE_SPECIFIER_BIT_INT))
			return EINVAL;
		/* bit-int can be used with signed/unsigned */
		mask &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		mask &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_TYPE_SPECIFIER_BIT_INT);
	}

	if (type == CC_TOKEN_BOOL) {
		/* Can't have more than one */
		if (bits_get(mask, CC_TYPE_SPECIFIER_BOOL))
			return EINVAL;
		/* bool can't be used with others */
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_TYPE_SPECIFIER_BOOL);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_STRUCT) {
		/* Can't have more than one */
		if (bits_get(mask, CC_TYPE_SPECIFIER_STRUCT))
			return EINVAL;
		/* Can't be used with others */
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_TYPE_SPECIFIER_STRUCT);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_UNION) {
		/* Can't have more than one */
		if (bits_get(mask, CC_TYPE_SPECIFIER_UNION))
			return EINVAL;
		/* Can't be used with others */
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_TYPE_SPECIFIER_UNION);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_ENUM) {
		/* Can't have more than one */
		if (bits_get(mask, CC_TYPE_SPECIFIER_ENUM))
			return EINVAL;
		/* Can't be used with others */
		if (mask)
			return EINVAL;
		this->mask |= bits_on(CC_TYPE_SPECIFIER_ENUM);
		return ESUCCESS;
	}
	assert(0);	/* TODO */
	return EINVAL;
}

static
err_t cc_node_add_type_specifier(struct cc_node *this,
								 const enum cc_token_type type)
{
	struct cc_node_type_specifiers *ts;
	ts = cc_node_assert_type(this, CC_NODE_TYPE_SPECIFIERS);
	return cc_node_type_specifiers_add(ts, type);
}
/*****************************************************************************/
static
err_t cc_node_add_symbol(struct cc_node *this,
						 struct cc_node *node)
{
	const enum cc_node_type type = cc_node_type(node);
	struct cc_node_symbols *ss = cc_node_assert_type(this, CC_NODE_SYMBOLS);
	struct cc_node_symbol *s = cc_node_assert_type(node, type);
	struct ptr_queue *q = &ss->entries[s->name_space];
	return ptrq_add_tail(q, node);
}
/*****************************************************************************/
static
err_t cc_node_symbols_find_type_def(const struct cc_node_symbols *this,
									const char *name,
									struct ptr_queue *out)
{
	int i;
	err_t err;
	const struct ptr_queue *q;
	struct cc_node *n;
	struct cc_node_identifier *ident;
	struct cc_node_symbol *s;
	const enum cc_name_space_type ns = CC_NAME_SPACE_ORDINARY;
	/* type-def-names are in ordinary name-space */

	q = &this->entries[ns];
	PTRQ_FOR_EACH(q, i, n) {
		assert(cc_node_is_symbol(n));	/* only symbols in a symtab */
		if (cc_node_type(n) != CC_NODE_SYMBOL_TYPE_DEF)
			continue;
		s = cc_node_assert_type(n, CC_NODE_SYMBOL_TYPE_DEF);

		n = s->identifier;
		assert(n);
		ident = cc_node_assert_type(n, CC_NODE_IDENTIFIER);
		assert(ident->string);
		if (strcmp(ident->string, name))
			continue;
		err = ptrq_add_tail(out, s);
		if (err)
			return err;
	}
	return ESUCCESS;
}

/* out initialized by caller */
static
err_t cc_node_find_type_def(struct cc_node *this,
							const char *name,
							struct ptr_queue *out)
{
	err_t err;
	const struct cc_node_symbols *ss;

	while (this) {
		ss = cc_node_assert_type(this, CC_NODE_SYMBOLS);
		err = cc_node_symbols_find_type_def(ss, name, out);
		if (err)
			return err;
		this = cc_node_parent(this);
	}
	if (ptrq_num_entries(out) == 0)
		return ENOENT;
	return ESUCCESS;
}
/*****************************************************************************/
err_t parser_build_types(struct parser *this)
{
	err_t err;
	int i;
	struct cc_node *n[2];
	struct cc_node_symbols *ss;
	struct cc_node_symbol *s;
	struct cc_node_type_integer *ti;
	static const enum cc_node_type types[] = {
		CC_NODE_TYPE_VOID,

		CC_NODE_TYPE_BOOL,
		CC_NODE_TYPE_CHAR,
		CC_NODE_TYPE_SHORT,
		CC_NODE_TYPE_INT,
		CC_NODE_TYPE_LONG,
		CC_NODE_TYPE_LONG_LONG,
	};
	static const struct cc_node_type_integer ints[] = {
		{0, 0, 0, 0},		/* unused */

		{8, 1, 7, 8},		/* bool */
		{8, 7, 0, 8},		/* char */
		{16, 15, 0, 16},	/* short */
		{32, 31, 0, 32},	/* int */
		{64, 63, 0, 64},	/* long */
		{64, 63, 0, 64},	/* long long */
	};

	/*
	 * The type for char will have a child with the name 'signed' since, char
	 * and signed-char are different, incompatible types. Other int types are
	 * all 'signed', hence they do not need an extra 'signed' child.
	 */

	/* current scope should be FILE */
	ss = cc_node_assert_type(this->symbols, CC_NODE_SYMBOLS);
	assert(cc_node_symbols_scope(ss) == CC_SCOPE_FILE);
	for (i = 0; i < (int)ARRAY_SIZE(types); ++i) {
		if (i > 0)
			n[0] = cc_node_new_type_integer(types[i]);
		else
			n[0] = cc_node_new(types[i]);	/* CC_NODE_TYPE_VOID */
		n[1] = cc_node_new_symbol(this->symbols, CC_NODE_SYMBOL_TYPE);
		if (n[0] == NULL || n[1] == NULL)
			return ENOMEM;
		if (i > 0) {
			ti = cc_node_assert_type(n[0], types[i]);
			*ti = ints[i];
		}
		s = cc_node_assert_type(n[1], CC_NODE_SYMBOL_TYPE);
		s->type = n[0];		/* For types, value == type. */
		s->linkage = CC_LINKAGE_NONE;	/* types have no linkages */
		s->storage = CC_STORAGE_NONE;	/* types have no storage duration */
		s->name_space = CC_NAME_SPACE_ORDINARY;
		err = cc_node_add_symbol(this->symbols, n[1]);
		if (err)
			return err;
	}
	return err;
}

err_t parser_new(const char *path,
				 struct parser **out)
{
	err_t err;
	int fd, ret;
	const char *buffer;
	size_t size;
	struct parser *this;
	struct stat stat;

	err = ESUCCESS;
	this = malloc(sizeof(*this));
	if (this == NULL)
		return ENOMEM;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		err = errno;
		goto err0;
	}

	ret = fstat(fd, &stat);
	if (ret < 0) {
		err = errno;
		goto err1;
	}

	size = stat.st_size;
	if (size == 0) {
		err = EINVAL;
		goto err1;
	}

	buffer = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (buffer == MAP_FAILED) {
		err = errno;
		goto err1;
	}
	this->cpp_tokens_path = path;
	this->cpp_tokens_fd = fd;
	this->root = NULL;
	this->symbols = cc_node_new_symbols(CC_SCOPE_FILE);
	if (this->symbols == NULL) {
		err = ENOMEM;
		goto err1;
	}
	err = parser_build_types(this);
	if (err)
		goto err1;
	cc_token_stream_init(&this->stream, buffer, size);
	*out = this;
	return ESUCCESS;
err1:
	close(fd);
err0:
	return err;
}

static
void parser_cleanup0(struct parser *this)
{
	assert(this);
	munmap((void *)this->stream.buffer, this->stream.buffer_size);
	close(this->cpp_tokens_fd);
	unlink(this->cpp_tokens_path);
	free((void *)this->cpp_tokens_path);
	cc_token_stream_empty(&this->stream);
}

err_t parser_delete(struct parser *this)
{
	assert(this);
	cc_node_delete(this->root);
	cc_node_delete(this->symbols);
	free(this);
	return ESUCCESS;
}
/*****************************************************************************/
static
err_t cc_token_convert_radix(struct cc_token *this,
							 const int radix)
{
	bool was_prev_separator, dot_seen;
	const char *str;
	size_t i, str_len, num_digits;
	this->type = CC_TOKEN_INTEGER_CONST;

	str = cc_token_string(this);
	str_len = cc_token_string_length(this);

	num_digits = 0;
	i = 0;
	if (radix == 2 || radix == 16)
		i = 2;
	if (i >= str_len)
		return EINVAL;
	was_prev_separator = dot_seen = false;
	num_digits = 0;
digits:
	if (i == str_len)
		goto done;

	if (is_digit_in_radix(str[i], radix)) {
		was_prev_separator = false;
		++num_digits;
		++i;
		goto digits;
	}

	if (str[i] == '\'') {
		/*
		 * can't have separator without digits
		 * can't have consec. separators.
		 * can't have seperator ending the sequence.
		 */
		if (num_digits == 0 || was_prev_separator || i == str_len)
			return EINVAL;
		was_prev_separator = true;
		++i;
		goto digits;
	}

	/*
	 * it is okay to not have digits before ., but then there must be at
	 * least one digit after .
	 * if there are digits before dot, then it is okay to not have digits
	 * after dot.
	 */
	if (str[i] == '.') {
		/*
		 * can't have more than one dots.
		 * can't have dot after the separator.
		 * can't have dots in oct/bin numbers
		 * can't have . after an exp.
		 */
		if (dot_seen || was_prev_separator || radix == 2 || radix == 8)
			return EINVAL;

		/* dot with nothing before it; it must have >= 1 digits after. */
		if (num_digits == 0 &&
			(i == str_len - 1 || !is_digit_in_radix(str[i + 1], radix)))
			return EINVAL;

		this->type = CC_TOKEN_FLOATING_CONST;
		dot_seen = true;
		num_digits = 0;	/* Can't have seperator imm. follow the dot */
		was_prev_separator = false;
		++i;
		goto digits;
	}

	if (str[i] == 'e' || str[i] == 'E') {
		if (radix != 10)
			return EINVAL;
		goto exponent;
	}

	if (str[i] == 'p' || str[i] == 'P') {
		if (radix != 16)
			return EINVAL;
		goto exponent;
	}

	/* float-suffix without exp. dot-seen must be true. */
	if (dot_seen &&
		(str[i] == 'd' || str[i] == 'f' || str[i] == 'l' ||
		 str[i] == 'D' || str[i] == 'F' || str[i] == 'L'))
		goto floating_suffix;

	/* integer-suffix. dot-seen must be false. */
	if (!dot_seen &&
		(str[i] == 'u' || str[i] == 'U' || str[i] == 'l' || str[i] == 'L' ||
		 str[i] == 'w' || str[i] == 'W'))
		goto integer_suffix;
	return EINVAL;
exponent:
	this->type = CC_TOKEN_FLOATING_CONST;
	if (num_digits == 0 && dot_seen == false)
		return EINVAL;
	assert(radix == 10 || radix == 16);
	assert(str[i] == 'e' || str[i] == 'E' || str[i] == 'p' || str[i] == 'P');
	if (++i == str_len)
		return EINVAL;	/* There must be digits after exp */
	if (str[i] == '+' || str[i] == '-')
		++i;
	num_digits = 0;
	for (; i < str_len; ++i) {
		if (!is_digit_in_radix(str[i], radix))
			break;
		++num_digits;
	}
	if (num_digits == 0)
		return EINVAL;	/* There must be at least one digit after exp. */
	/* fall-thru */
floating_suffix:
	this->type = CC_TOKEN_FLOATING_CONST;
	if (i == str_len)
		goto done;
	/*return ESUCCESS;*/
	if (str[i] == 'f' || str[i] == 'F' ||
		str[i] == 'l' || str[i] == 'L') {
		++i;
		goto done;
	}
	if (str[i] != 'd' && str[i] != 'D')
		return EINVAL;
	++i;
	if (i == str_len)
		return EINVAL;
	if (str[i] == 'f' || str[i] == 'F' ||
		str[i] == 'd' || str[i] == 'D' ||
		str[i] == 'l' || str[i] == 'L') {
		++i;
		goto done;
	}
	return EINVAL;
integer_suffix:
	if (str[i] == 'u' || str[i] == 'U')
		goto unsigned_suffix_begin;
	if (str[i] == 'l' || str[i] == 'L')
		goto long_suffix_begin;
	if (str[i] == 'w' || str[i] == 'W')
		goto bit_precise_int_suffix_begin;
	return EINVAL;
unsigned_suffix_begin:
	/* Consume u/U, check for the those that can follow */
	if (++i == str_len)
		goto done;
	if (str[i] == 'l' || str[i] == 'L')
		goto long_suffix_end;
	if (str[i] == 'w' || str[i] == 'W')
		goto bit_precise_int_suffix_end;
	return EINVAL;
long_suffix_begin:
	if (++i == str_len)
		goto done;
	if (str[i - 1] == 'l' && str[i] == 'l')
		++i;
	else if (str[i - 1] == 'L' && str[i] == 'L')
		++i;
	if (i == str_len)
		goto done;
	if (str[i] == 'u' || str[i] == 'U')
		goto unsigned_suffix_end;
	if (str[i] == 'w' || str[i] == 'W')
		goto bit_precise_int_suffix_end;
	return EINVAL;
bit_precise_int_suffix_begin:
	if (++i == str_len)
		return EINVAL;
	if (str[i - 1] == 'w' && str[i] == 'b')
		++i;
	else if (str[i - 1] == 'W' && str[i] == 'B')
		++i;
	if (i == str_len)
		goto done;
	if (str[i] == 'u' || str[i] == 'U')
		goto unsigned_suffix_end;
	if (str[i] == 'l' || str[i] == 'L')
		goto long_suffix_end;
	return EINVAL;
unsigned_suffix_end:
	if (++i == str_len)
		goto done;
	return EINVAL;
long_suffix_end:
	if (++i == str_len)
		goto done;
	if (str[i - 1] == 'l' && str[i] == 'l')
		++i;
	else if (str[i - 1] == 'L' && str[i] == 'L')
		++i;
	if (i == str_len)
		goto done;
	return EINVAL;
bit_precise_int_suffix_end:
	if (++i == str_len)
		return EINVAL;
	if (str[i - 1] == 'w' && str[i] == 'b')
		++i;
	else if (str[i - 1] == 'W' && str[i] == 'B')
		++i;
	if (i == str_len)
		goto done;
	return EINVAL;
done:
	if (i != str_len || was_prev_separator)
		return EINVAL;
	if (num_digits == 0 && dot_seen == false)
		return EINVAL;
	return ESUCCESS;
}

/* converts from pp-number to one of the int/float consts. */
static
err_t cc_token_convert_number(struct cc_token *this)
{
	const char *str;
	size_t i, str_len;

	str = cc_token_string(this);
	str_len = cc_token_string_length(this);
	i = 0;

	/* oct, hex or bin */
	if (str[i] == '0') {
		++i;
		if (i == str_len) {
			this->type = CC_TOKEN_INTEGER_CONST;
			return ESUCCESS;
		}

		/* bin/oct can't have fractional consts, but hex can */
		if (str[i] == 'x' || str[i] == 'X')
			return cc_token_convert_radix(this, 16);
		if (str[i] == 'b' || str[i] == 'B')
			return cc_token_convert_radix(this, 2);
		return cc_token_convert_radix(this, 8);
	}
	return cc_token_convert_radix(this, 10);
}

/* uint32_t, uint64_t, string bytes */
static
err_t cc_token_convert(struct cc_token *this)
{
	err_t err;
	enum cc_token_type type;

	/*
	 * TODO convert string-literals and char-consts to their exec-char-set
	 * representation. The char-consts and numbers will also need evaluation;
	 * compiler optimizations depend on such const-values to perform
	 * effectively.
	 */
	err = ESUCCESS;
	type = cc_token_type(this);
	if (type == CC_TOKEN_NUMBER)
		err = cc_token_convert_number(this);
	return err;
}
/*****************************************************************************/
static
err_t cc_token_stream_read_token(struct cc_token_stream *this,
								 struct cc_token **out)
{
	struct cc_token *token;
	bool is_ident;
	char *src;
	size_t src_len;
	enum cc_token_type type;	/* lxr_token_type == cc_token_type */
	size_t position;

	position = this->position;
	if (position >= this->buffer_size)
		return EOF;

	assert(position < this->buffer_size);
	assert(sizeof(type) == 4);
	memcpy(&type, &this->buffer[position], sizeof(type));
	position += sizeof(type);
	token = malloc(sizeof(*token));
	if (token == NULL)
		return ENOMEM;
	token->type = type;

	/* cc_token_type_is_key_word only checks for c-key-words */
	if (cc_token_type_is_key_word(type) ||
		cc_token_type_is_punctuator(type)) {
		/* These are lexer-key-words and punctuators. No src-len. */
		token->string = NULL;
		token->string_len = 0;
		goto done;
	}

	/*
	 * These ranges of lexer-key-words are converted into identifiers:
	 * [LXR_TOKEN_NO_RETURN, LXR_TOKEN_REPRODUCIBLE]
	 * [LXR_TOKEN_DIRECTIVE_DEFINE, LXR_TOKEN_DIRECTIVE_WARNING]
	 */
	is_ident = false;
	if (!is_ident &&
		type >= CC_TOKEN_NO_RETURN &&
		type <= CC_TOKEN_REPRODUCIBLE)
		is_ident = true;
	if (!is_ident &&
		type >= CC_TOKEN_DIRECTIVE_DEFINE &&
		type <= CC_TOKEN_DIRECTIVE_WARNING)
		is_ident = true;
	if (is_ident) {
		/* These are lexer-key-words. No src-len */
		token->string = src = strdup(g_key_words[type - CC_TOKEN_ATOMIC]);
		if (src == NULL)
			return ENOMEM;
		token->string_len = strlen(token->string);
		token->type = CC_TOKEN_IDENTIFIER;
		goto done;
	}
	memcpy(&src_len, &this->buffer[position], sizeof(src_len));
	assert(src_len);

	position += sizeof(src_len);
	src = NULL;
	src = malloc(src_len + 1);
	if (src == NULL)
		return ENOMEM;
	src[src_len] = 0;
	memcpy(src, &this->buffer[position], src_len);
	position += src_len;
	token->string = src;
	token->string_len = src_len;
done:
	assert(position > this->position);
	this->position = position;
	*out = token;
	return ESUCCESS;
}

static
err_t cc_token_stream_peek_entry(struct cc_token_stream *this,
								 const int off,
								 struct cc_token **out)
{
	err_t err;
	int num_entries;
	struct cc_token *token;

	num_entries = ptrq_num_entries(&this->q);
	assert(0 <= off && off <= num_entries);
	if (off < num_entries) {
		*out = ptrq_peek_entry(&this->q, 0);
		return ESUCCESS;
	}

	/* Not reading from the cpp_tokens file? EOF */
	if (this->buffer == NULL)
		return EOF;
	err = cc_token_stream_read_token(this, &token);
	if (!err)
		err = cc_token_convert(token);
	if (!err)
		err = ptrq_add_tail(&this->q, token);
	if (!err)
		*out = token;
	return err;
}

static
err_t cc_token_stream_peek_head(struct cc_token_stream *this,
								struct cc_token **out)
{
	return cc_token_stream_peek_entry(this, 0, out);
}

static
err_t cc_token_stream_remove_head(struct cc_token_stream *this,
								  struct cc_token **out)
{
	err_t err;
	struct cc_token *token;

	err = cc_token_stream_peek_head(this, out);
	if (err)
		return err;
	token = ptrq_remove_head(&this->q);
	assert(token == *out);
	return err;
}
/*****************************************************************************/
static
bool parser_has_attributes(struct parser *this)
{
	err_t err;
	struct cc_token_stream *stream;
	struct cc_token *token;

	stream = parser_token_stream(this);
	err = cc_token_stream_peek_entry(stream, 0, &token);
	if (err || cc_token_type(token) != CC_TOKEN_LEFT_BRACKET)
		return false;
	err = cc_token_stream_peek_entry(stream, 1, &token);
	if (err || cc_token_type(token) != CC_TOKEN_LEFT_BRACKET)
		return false;
	return true;
}
/*****************************************************************************/
static
err_t parser_parse_static_assert_declaration(struct parser *this,
											 struct cc_node *parent)
{
	assert(0);
	(void)this;
	(void)parent;
	return ENOTSUP;
}

/* Attributes can apply to both types and identifiers */
static
err_t parser_parse_attribute_specifiers(struct parser *this,
										struct cc_node **out)
{
	assert(0);
	(void)this;
	(void)out;
	return ENOTSUP;
}
/*****************************************************************************/
static
err_t parser_parse_type_specifier_atomic(struct parser *this,
										 struct cc_node *node)
{
	assert(0);
	(void)this;
	(void)node;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier_bit_int(struct parser *this,
										  struct cc_node *node)
{
	assert(0);
	(void)this;
	(void)node;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier_struct(struct parser *this,
										 struct cc_node *node)
{
	assert(0);
	(void)this;
	(void)node;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier_enum(struct parser *this,
									   struct cc_node *node)
{
	assert(0);
	(void)this;
	(void)node;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier_type_of(struct parser *this,
										  struct cc_node *node)
{
	assert(0);
	(void)this;
	(void)node;
	return ENOTSUP;
}

/* single-token type-specifiers */
static
err_t parser_parse_type_specifier_single(struct parser *this,
										 struct cc_node *node)
{
	assert(0);
	(void)this;
	(void)node;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier(struct parser *this,
								  struct cc_node **out)
{
	err_t err;
	enum cc_token_type type;
	struct cc_token_stream *stream;
	struct cc_token *token;

	if (out[0] == NULL)
		out[0] = cc_node_new_type_specifiers();
	if (out[0] == NULL)
		return ENOMEM;

	stream = parser_token_stream(this);
	cc_node_assert_type(out[0], CC_NODE_TYPE_SPECIFIERS);
	err = cc_token_stream_peek_head(stream, &token);
	assert(err == ESUCCESS);
	type = cc_token_type(token);

	/* Try to update the bitmask, and validate this type-spec. */
	err = cc_node_add_type_specifier(out[0], type);
	if (err)
		return err;

	if (type == CC_TOKEN_ATOMIC)
		return parser_parse_type_specifier_atomic(this, out[0]);
	if (type == CC_TOKEN_BIT_INT)
		return parser_parse_type_specifier_bit_int(this, out[0]);
	if (type == CC_TOKEN_ENUM)
		return parser_parse_type_specifier_enum(this, out[0]);
	if (type == CC_TOKEN_STRUCT ||
		type == CC_TOKEN_UNION)
		return parser_parse_type_specifier_struct(this, out[0]);
	if (type == CC_TOKEN_TYPE_OF ||
		type == CC_TOKEN_TYPE_OF_UNQUAL)
		return parser_parse_type_specifier_type_of(this, out[0]);
	return parser_parse_type_specifier_single(this, out[0]);
}
/*****************************************************************************/
static
err_t parser_parse_type_qualifier(struct parser *this,
								  struct cc_node **out)
{
	err_t err;
	enum cc_token_type type;
	struct cc_token_stream *stream;
	struct cc_token *token;

	if (out[0] == NULL)
		out[0] = cc_node_new_type_qualifiers();
	if (out[0] == NULL)
		return ENOMEM;
	stream = parser_token_stream(this);
	cc_node_assert_type(out[0], CC_NODE_TYPE_QUALIFIERS);

	err = cc_token_stream_remove_head(stream, &token);
	assert(err == ESUCCESS);
	type = cc_token_type(token);
	cc_token_delete(token);

	/* Try to update the bitmask */
	return cc_node_add_type_qualifier(out[0], type);
}

static
err_t parser_parse_alignment_specifier(struct parser *this,
									   int *out)
{
	assert(0);
	(void)this;
	(void)out;
	return ENOTSUP;
}

static
err_t parser_parse_storage_specifier(struct parser *this,
									 struct cc_node **out)
{
	err_t err;
	enum cc_token_type type;
	struct cc_token_stream *stream;
	struct cc_token *token;

	if (out[0] == NULL)
		out[0] = cc_node_new_storage_specifiers();
	if (out[0] == NULL)
		return ENOMEM;
	stream = parser_token_stream(this);
	cc_node_assert_type(out[0], CC_NODE_STORAGE_SPECIFIERS);

	err = cc_token_stream_remove_head(stream, &token);
	assert(err == ESUCCESS);
	type = cc_token_type(token);
	cc_token_delete(token);

	/* Try to update the bitmask */
	return cc_node_add_storage_specifier(out[0], type);
}

static
err_t parser_parse_function_specifier(struct parser *this,
									  struct cc_node **out)
{
	assert(0);
	(void)this;
	(void)out;
	return ENOTSUP;
}
/*****************************************************************************/
static
err_t parser_parse_declaration_specifier(struct parser *this,
										 struct cc_node *parent)
{
	err_t err;
	int i;
	struct cc_node_declaration_specifiers *ds;
	struct cc_token_stream *stream;
	struct cc_token *tokens[2];
	struct cc_node **nodes[4];
	/* typedef err_t func(struct parser *, struct cc_node *); */
	/* static func * const funcs[4] = */
	static err_t (* const funcs[4])(struct parser *, struct cc_node **) = {
		parser_parse_type_specifier,
		parser_parse_type_qualifier,
		parser_parse_function_specifier,
		parser_parse_storage_specifier,
	};
	stream = parser_token_stream(this);
	ds = cc_node_assert_type(parent, CC_NODE_DECLARATION_SPECIFIERS);
	err = cc_token_stream_peek_head(stream, &tokens[0]);
	assert(err == ESUCCESS);
	nodes[0] = &ds->type_specifiers;
	nodes[1] = &ds->type_qualifiers;
	nodes[2] = &ds->function_specifiers;
	nodes[3] = &ds->storage_specifiers;

	if (cc_token_is_alignment_specifier(tokens[0]))
		return parser_parse_alignment_specifier(this, &ds->alignment);

	if (cc_token_is_type_specifier(tokens[0]))
		i = 0;
	else if (cc_token_is_type_qualifier(tokens[0]))
		i = 1;
	else if (cc_token_is_function_specifier(tokens[0]))
		i = 2;
	else if (cc_token_is_storage_specifier(tokens[0]))
		i = 3;
	else
		return EINVAL;

	if (cc_token_type(tokens[0]) == CC_TOKEN_ATOMIC) {
		/* Atomic may represent either a TypeSpecifier or a TypeQualifier */
		assert(i == 0);	/* Because check for type_specifier is first */
		err = cc_token_stream_peek_entry(stream, 1, &tokens[1]);
		if (err || cc_token_type(tokens[1]) != CC_TOKEN_LEFT_PAREN)
			i = 1;
	}
	return funcs[i](this, nodes[i]);
}
/*****************************************************************************/
static
err_t parser_parse_declaration_specifiers(struct parser *this,
										  struct cc_node **out)
{
	err_t err;
	bool is_specifier;
	const char *str;
	struct ptr_queue stes;
	const struct cc_node *ste;
	struct cc_node *attributes;
	struct cc_token_stream *stream;
	struct cc_token *token;

	/*
	 * array of DeclarationSpecifier elements. The array may optionally end
	 * with an AttributeSpecifierSequence
	 */
	ptrq_init(&stes, NULL);
	stream = parser_token_stream(this);

	out[0] = cc_node_new_declaration_specifiers();
	if (out[0] == NULL)
		return ENOMEM;

	while (true) {
		/* We do not expect an error, not even an EOF */
		err = cc_token_stream_peek_head(stream, &token);
		if (err)
			return err;

		is_specifier = false;
		if (cc_token_is_type_specifier(token) ||
			cc_token_is_type_qualifier(token) ||
			cc_token_is_alignment_specifier(token) ||
			cc_token_is_storage_specifier(token) ||
			cc_token_is_function_specifier(token))
			is_specifier = true;
		if (!is_specifier)
			break;

		/* Determine if this (non-keyword) Identifier is a TypedefName */
		if (cc_token_is_identifier(token)) {
			/* Should be a TypedefName. If not, break */
			str = cc_token_string(token);
			assert(str);
			assert(this->symbols);
			err = cc_node_find_type_def(this->symbols, str, &stes);
			if (err == ENOENT) {
				err = ESUCCESS;
				break;	/* Not a TypedefName */
			}
			if (err)
				return err;
			/* Free stes */
			PTRQ_FOR_EACH_WITH_REMOVE(&stes, ste);
			/* this is indeed a TypedefName */
		}
		err = parser_parse_declaration_specifier(this, out[0]);
		if (err)
			return err;

		/*
		 * If an AttributeSpecifierSequence follows, that marks the end of the
		 * DeclarationSpecifiers.
		 * These attributes apply to the type specified by the preceding
		 * specifiers.
		 */
		if (!parser_has_attributes(this))
			continue;
		attributes = NULL;
		err = parser_parse_attribute_specifiers(this, &attributes);
		if (!err)
			err = cc_node_add_tail_child(out[0], attributes);
		if (err)
			return err;
		break;
	}
	assert(err == ESUCCESS);
	return err;
}
/*****************************************************************************/
/*
 * attributes are the Declaration attributes; they apply to the identifier
 * in the Declarator. The attributes are stored as the child of the symbol
 * node. We cannot store it as a child of symbol.identifier, as identifier may
 * be NULL.
 * The attributes within the DeclarationSpecifiers apply to the type.
 * symbol.prev is used to link same-name type-defs found in the same scope.
 */
static
err_t parser_process_declaration_type_def(struct parser *this,
										  struct cc_node *attributes,
										  struct cc_node *specifiers,
										  struct cc_node *declarator)
{
	assert(0);
	(void)this;
	(void)attributes;
	(void)specifiers;
	(void)declarator;
	return ENOTSUP;
}

/*
 * A Declaration ... shall declare at least a declarator(other than
 * declarators that are params of a func, or are members of struct/union),
 * a tag, or a member of  enumeration.
 *
 * struct { int a; }; is invalid - it has no Declarator, its
 * DeclarationSpecifiers contain an unnamed struct (hence no tag), and its no
 * enum.
 *
 * enum; is invalid - it has no Declarator; its DeclarationSpecifiers contains
 * an unnamed enum with no members.
 *
 * Thus, if the DeclarationSpecifiers has a tag, then the above condn is
 * satisfied.
 * Else, there's no tag. Then, if the DeclarationSpecifiers is an unnamed enum
 * with a constant, then the condn is satisfied.
 * Else, if there's a Declarator then the condn is satisfied.
 * Else, the condn is not satsified.
 */
static
err_t parser_process_declaration(struct parser *this,
								 struct ptr_queue *nodes)
{
	err_t err;
	bool is_type_def;
	struct cc_node *attributes, *specifiers, *declarator;
	struct cc_node *storage_specifiers;
	struct cc_node_declaration_specifiers *ds;

	specifiers = NULL;
	assert(ptrq_num_entries(nodes));
	attributes = ptrq_remove_head(nodes);
	assert(attributes);
	if (cc_node_type(attributes) == CC_NODE_DECLARATION_SPECIFIERS) {
		specifiers = attributes;
		attributes = NULL;
	} else if (cc_node_type(attributes) != CC_NODE_ATTRIBUTES) {
		return EINVAL;
	}
	if (specifiers == NULL) {
		assert(attributes);
		assert(ptrq_num_entries(nodes));
		specifiers = ptrq_remove_head(nodes);
	}
	/* There may be no Declarator. For e.g. struct abc;, enum def; */
	assert(specifiers);
	ds = cc_node_assert_type(specifiers, CC_NODE_DECLARATION_SPECIFIERS);
	storage_specifiers = ds->storage_specifiers;
	assert(storage_specifiers);
	is_type_def = cc_node_is_storage_specifier_type_def(storage_specifiers);

	/* There must be at least one Declarator for type-defs */
	if (is_type_def)
		assert(ptrq_num_entries(nodes));
	else
		assert(0);	/* TODO */

	/* For each declarator */
	while (!ptrq_is_empty(nodes)) {
		declarator = ptrq_remove_head(nodes);
		assert(cc_node_type(declarator) == CC_NODE_DECLARATOR);
		if (is_type_def)
			err = parser_process_declaration_type_def(this, attributes,
													  specifiers, declarator);
		if (err)
			return err;
	}
	return ENOTSUP;
}

/*
 * This is not a function-definition.
 * It receives attributes, specifiers, and one declarator.
 */
static
err_t parser_parse_declaration(struct parser *this,
							   struct ptr_queue *nodes)
{
	err_t err;
	struct cc_token *token;
	struct cc_token_stream *stream;

	stream = parser_token_stream(this);
	while (true) {
		err = cc_token_stream_peek_head(stream, &token);
		if (err)
			return err;
		/* A semi-colon ends the Declaration */
		if (cc_token_type(token) == CC_TOKEN_SEMI_COLON) {
			err = cc_token_stream_remove_head(stream, &token);
			assert(err == ESUCCESS);
			cc_token_delete(token);
			return parser_process_declaration(this, nodes);
		}
	}
	// attributes, specifiers and one declarator.
	// If specifiers have inline, then the given declarator and those that
	// follow must all be for funct-types.
	return ENOTSUP;
}
/*****************************************************************************/
static
err_t parser_parse_function_definition(struct parser *this,
									   struct ptr_queue *nodes)
{
	assert(0);
	(void)this;
	(void)nodes;
	return ENOTSUP;
}
/*****************************************************************************/
static
err_t parser_parse_identifier(struct parser *this,
							  struct cc_node **out)
{
	err_t err;
	struct cc_token_stream *stream;
	struct cc_token *token;

	assert(out[0] == NULL);
	stream = parser_token_stream(this);

	err = cc_token_stream_remove_head(stream, &token);
	assert(err == ESUCCESS);
	assert(cc_token_is_identifier(token));
	out[0] = cc_node_new_identifier(cc_token_string(token),
									cc_token_string_length(token));
	if (out[0] == NULL)
		return ENOMEM;
	cc_token_reset_string(token);
	cc_token_delete(token);
	return err;
}
/*****************************************************************************/
static
err_t parser_parse_type_pointer(struct parser *this,
								struct cc_node **out)
{
	err_t err;
	struct cc_token_stream *stream;
	struct cc_token *token;
	struct cc_node *node, *attributes;

	assert(out[0] == NULL);
	stream = parser_token_stream(this);

	err = cc_token_stream_remove_head(stream, &token);
	assert(err == ESUCCESS);
	assert(cc_token_type(token) == CC_TOKEN_MUL);
	cc_token_delete(token);

	out[0] = cc_node_new_type_pointer();
	if (out[0] == NULL)
		return ENOMEM;

	/* Are there any AttributeSpecifiers for this pointer? */
	if (parser_has_attributes(this)) {
		/*
		 * These attributes appertain to the pointer, and not to the
		 * pointed-to object.
		 */
		attributes = NULL;
		err = parser_parse_attribute_specifiers(this, &attributes);
		if (!err)
			err = cc_node_add_tail_child(out[0], attributes);
		if (err)
			return err;
		/* fallthrough */
	}

	/* Are there any TypeQualifiers? Add the group as a child. */
	err = cc_token_stream_peek_head(stream, &token);
	if (err || !cc_token_is_type_qualifier(token))
		return ESUCCESS;	/* error handled later */

	/* Collect all the TypeQualifiers into a single child */
	node = NULL;
	while (true) {
		err = cc_token_stream_peek_head(stream, &token);
		if (err || !cc_token_is_type_qualifier(token))
			break;	/* Any err handled later */
		err = parser_parse_type_qualifier(this, &node);
		if (err)
			return err;
	}
	return cc_node_add_tail_child(out[0], node);
}
/*****************************************************************************/
/*
 * Caters to both ArrayDeclarator and AbstractArrayDeclarator.
 * The caller must verify
 */
static
err_t parser_parse_type_array(struct parser *this,
							  struct cc_node *ele_type,
							  struct cc_node **out)
{
	assert(0);
	(void)this;
	(void)ele_type;
	(void)out;
	/* TODO: AttributeSpecifierSequence */
	/* These attributes appertain to the array type, not the array name. */
	return ENOTSUP;
}
/*****************************************************************************/
/*
 * The unnamed param of type void is allowed only if it is the only parameter.
 * A parameter whose type is incomplete may have to be warned about.
 */
static
err_t parser_process_parameter_declaration(struct parser *this,
										   struct cc_node *nodes[])
{
	assert(0);
	(void)this;
	(void)nodes;
	return ENOTSUP;
}

/* The function updates the symbol table instead of returning any objects. */
static
err_t parser_parse_parameter_declaration(struct parser *this)
{
	err_t err;
	struct cc_token_stream *stream;
	struct cc_token *token;
	struct cc_node *attributes;
	struct cc_node *specifiers;
	struct cc_node *declarator;
	struct cc_node *nodes[4];

	stream = &this->stream;

	/* Is it an AttributeDeclaration? */
	attributes = specifiers = declarator = NULL;
	if (parser_has_attributes(this)) {
		err = parser_parse_attribute_specifiers(this, &attributes);
		if (err)
			return err;
	}

	/* Only DeclarationSpecifiers is guaranteed to be present */
	err = parser_parse_declaration_specifiers(this, &specifiers);
	if (!err)
		cc_node_assert_type(specifiers, CC_NODE_DECLARATION_SPECIFIERS);
	if (!err)
		assert(cc_node_num_children(specifiers) == 5);
	if (!err)
		err = cc_token_stream_peek_head(stream, &token);
	/* If a comma or a right-paren follows, then there's no Declarator */
	if (!err &&
		cc_token_type(token) != CC_TOKEN_COMMA &&
		cc_token_type(token) != CC_TOKEN_RIGHT_PAREN)
		err = parser_parse_declarator(this, &declarator);
	nodes[0] = attributes;	/* may be null */
	nodes[1] = specifiers;
	nodes[2] = declarator;
	nodes[3] = NULL;
	if (!err)
		err = parser_process_parameter_declaration(this, nodes);
	return err;
}

/*
 * Caters to both FunctionDeclarator and AbstractFunctionDeclarator.
 * The caller must verify.
 * Parses only the prototype. Begins at left-paren.
 */
static
err_t parser_parse_type_function(struct parser *this,
								 struct cc_node **out)
{
	err_t err;
	bool has_ellipsis;
	enum cc_token_type type;
	struct cc_token_stream *stream;
	struct cc_token *token;
	struct cc_node *symbols;
	struct cc_node_type_function *tf;
	struct cc_node_block *b;
	struct cc_node_symbols *ss;

	/*
	 * The ret-type is the actual type specified in the source, but 6.7.6.3
	 * ways that the ret-type must be unqualified and non-atomic; i.e., we must
	 * remove any type-qualifiers including _Atomic qualifier from the type.
	 * The resultant type is then the type of the function.
	 */
	assert(out[0] == NULL);
	out[0] = cc_node_new_type_function();
	if (out[0] == NULL)
		return ENOMEM;

	stream = parser_token_stream(this);
	err = cc_token_stream_remove_head(stream, &token);
	assert(err == ESUCCESS);
	assert(cc_token_type(token) == CC_TOKEN_LEFT_PAREN);
	cc_token_delete(token);

	tf = cc_node_assert_type(out[0], CC_NODE_TYPE_FUNCTION);
	b = cc_node_assert_type(tf->block, CC_NODE_BLOCK);
	ss = cc_node_assert_type(b->symbols, CC_NODE_SYMBOLS);
	assert(cc_node_symbols_scope(ss) == CC_SCOPE_PROTOTYPE);

	/*
	 * The function's symbol-table is a child of the current symtab.
	 * This helps in processing parameters, as that requires looking up
	 * types.
	 */
	err = cc_node_add_tail_child(this->symbols, b->symbols);
	if (err)
		return err;
	symbols = this->symbols;	/* Save the current symbol-table */
	this->symbols = b->symbols;	/* Set the new symbol-table */
	has_ellipsis = false;
	while (true) {
		err = cc_token_stream_remove_head(stream, &token);
		if (err)
			return err;
		type = cc_token_type(token);
		if (has_ellipsis && type != CC_TOKEN_RIGHT_PAREN)
			return EINVAL;
		if (type == CC_TOKEN_RIGHT_PAREN) {
			cc_token_delete(token);
			break;
		}
		if (type == CC_TOKEN_ELLIPSIS) {
			cc_token_delete(token);
			has_ellipsis = true;
			continue;
		}
		err = parser_parse_parameter_declaration(this);
		if (err)
			return err;
		err = cc_token_stream_peek_head(stream, &token);
		if (err)
			return err;
		type = cc_token_type(token);
		if (type == CC_TOKEN_RIGHT_PAREN)
			continue;
		if (type != CC_TOKEN_COMMA)
			return EINVAL;
		cc_token_delete(token);
	}
	/* TODO AttributeSpecifierSequence */
	/*
	 * These attributes appertain to the function-type and not the
	 * function-name/identifier.
	 */
	assert(err == ESUCCESS);
	this->symbols = symbols;	/* Revert back to the previous symtab */
	return err;
}
/*****************************************************************************/
static
err_t parser_parse_declarator_function(struct parser *this,
									   struct ptr_queue *list)
{
	err_t err;
	struct cc_node *node;

	/* ret-type of the function not yet known */
	node = NULL;
	err = parser_parse_type_function(this, &node);
	if (err)
		return err;
	/* TODO: Verify the structure of the node */
	return ptrq_add_tail(list, node);
}

static
err_t parser_parse_declarator_array(struct parser *this,
									struct ptr_queue *list)
{
	err_t err;
	struct cc_node *node;

	/* ele-type of the array not yet known */
	node = NULL;
	err = parser_parse_type_array(this, NULL, &node);
	if (err)
		return err;
	/* TODO: Verify the structure of the node */
	return ptrq_add_tail(list, node);
}

/*
 * The function should support parsing both Declarator and AbstractDeclarator,
 * Called from multiple contexts. It must monitor the arrival of either an
 * Identifier or the location where the omitted Identifier is supposed to be.
 * Depending on that, it must output either a Declarator or an
 * AbstractDeclarator. TODO convert asserts into syntax errors.
 */
static
err_t parser_parse_declarator(struct parser *this,
							  struct cc_node **out)
{
	err_t err;
	bool ident_found, is_abstract_delarator;
	struct ptr_queue stack;
	struct ptr_queue list;	/* output type-list. cc_symtab_entry * */
	enum cc_token_type type;
	struct cc_node_declarator *d;
	struct cc_token_stream *stream;
	struct cc_node *node;
	struct cc_token *token;

	assert(out[0] == NULL);	/* for now */

	stream = parser_token_stream(this);
	out[0] = cc_node_new_declarator();
	if (out[0] == NULL)
		return ENOMEM;

	/* The operand-stack need only store cc_token_type enums */
	ptrq_init(&stack, NULL);
	ptrq_init(&list, NULL);	/* destructor not needed; items moved */
	ident_found = false;
	is_abstract_delarator = false;
	while (true) {
		err = cc_token_stream_peek_head(stream, &token);
		if (err)
			return err;
		assert(!cc_token_is_key_word(token));
		type = cc_token_type(token);
		if (type == CC_TOKEN_IDENTIFIER) {
			/* Can't have two idents */
			if (ident_found)
				return EINVAL;
			ident_found = true;
			node = NULL;
			err = parser_parse_identifier(this, &node);
			if (!err)
				assert(ptrq_num_entries(&list) == 0);
			if (!err)
				err = ptrq_add_tail(&list, node);
			if (err)
				return err;
			continue;
		}

		/* If the stack is empty and we do not see a ( or [, then done */
		if (ptrq_is_empty(&stack) &&
			type != CC_TOKEN_LEFT_PAREN &&
			type != CC_TOKEN_LEFT_BRACKET)
			break;

		if (type == CC_TOKEN_MUL) {
			/* Can't have a Pointer after identifier is parsed. */
			if (ident_found)
				return EINVAL;
			node = NULL;
			err = parser_parse_type_pointer(this, &node);
			if (!err)
				err = ptrq_add_tail(&stack, node);
			if (err)
				return err;
			continue;
		}

		/* left-parent after identifier or its location has been known */
		if (type == CC_TOKEN_LEFT_PAREN && ident_found) {
			/* This is the params of a func-type. consumes the right-paren */
			err = parser_parse_declarator_function(this, &list);
			if (err)
				return err;
			/* No need to update was_prev_left_paren after ident is found. */
			continue;
		}

		/* left-bracket after identifier or its location has been known */
		if (type == CC_TOKEN_LEFT_BRACKET && ident_found) {
			/* consumes the right-bracket */
			err = parser_parse_declarator_array(this, &list);
			if (err)
				return err;
			continue;
		}

		/* right-paren after ident or its location has been known */
		if (type == CC_TOKEN_RIGHT_PAREN && ident_found) {
			err = cc_token_stream_remove_head(stream, &token);
			assert(err == ESUCCESS);
			cc_token_delete(token);

			node = NULL;
			while (ptrq_num_entries(&stack)) {
				node = ptrq_remove_tail(&stack);
				if (cc_node_type(node) == CC_NODE_LEFT_PAREN)
					break;
				err = ptrq_add_tail(&list, node);
				if (err)
					return err;
				node = NULL;
			}
			if (node == NULL)
				return EINVAL;
			cc_node_delete(node);	/* the left-paren */
			continue;
		}

		/* We have left-paren, and neither the identifier nor its location. */
		if (type == CC_TOKEN_LEFT_PAREN && !ident_found) {
			/*
			 * If a ( or a * follows this (, then continue.
			 * else, we now have a location of the identifier
			 */
			err = cc_token_stream_peek_entry(stream, 1, &token);
			if (err)
				return err;
			type = cc_token_type(token);
			if (type == CC_TOKEN_LEFT_PAREN ||
				type == CC_TOKEN_MUL) {
				/* This is a precedence-ordering paren */
				err = cc_token_stream_remove_head(stream, &token);
				assert(err == ESUCCESS);
				cc_token_delete(token);
				node = cc_node_new(CC_NODE_LEFT_PAREN);
				if (node == NULL)
					return ENOMEM;
				err = ptrq_add_tail(&stack, node);
				if (err)
					return err;
				continue;
			}

			/*
			 * The ident-location is to the imm-left of this (.
			 * Keep that token within the input, and rescan.
			 */
			ident_found = is_abstract_delarator = true;
			assert(ptrq_num_entries(&list) == 0);
			continue;
		}

		/* left-bracket, but neither the identifier nor its location. */
		if (type == CC_TOKEN_LEFT_BRACKET && !ident_found) {
			ident_found = is_abstract_delarator = true;
			assert(ptrq_num_entries(&list) == 0);
			/* The ident-locn is to the imm-left of the left-bracket */
			continue;	/* Retry parsing [ */
		}

		/* right-paren, but neither the identifier nor its location. */
		if (type == CC_TOKEN_RIGHT_PAREN && !ident_found) {
			ident_found = is_abstract_delarator = true;
			assert(ptrq_num_entries(&list) == 0);
			/* The ident-locn is to the imm-left of the right-paren */
			continue;	/* Retry parsing ) */
		}
		assert(0);
		return EINVAL;
	}
	assert(err == ESUCCESS);
	d = cc_node_assert_type(out[0], CC_NODE_DECLARATOR);
	if (is_abstract_delarator)
		out[0]->type = CC_NODE_ABSTRACT_DECLARATOR;
	return ptrq_move(&list, &d->list);
}
/*****************************************************************************/
static
err_t parser_parse_external_declaration(struct parser *this,
										struct cc_node *parent)
{
	err_t err;
	struct cc_token_stream *stream;
	struct cc_token *token;
	struct cc_node *attributes, *specifiers, *declarator;
	struct ptr_queue nodes;

	ptrq_init(&nodes, cc_node_delete);
	cc_node_assert_type(parent, CC_NODE_TRANSLATION_UNIT);

	/* Is it a static_assert declaration? */
	stream = &this->stream;
	err = cc_token_stream_peek_head(stream, &token);
	if (err)
		return err;
	if (cc_token_type(token) == CC_TOKEN_STATIC_ASSERT)
		return parser_parse_static_assert_declaration(this, parent);

	/* Is it an AttributeDeclaration? */
	attributes = NULL;
	if (parser_has_attributes(this)) {
		err = parser_parse_attribute_specifiers(this, &attributes);
		if (!err)
			err = cc_token_stream_peek_head(stream, &token);
		if (!err && cc_token_type(token) == CC_TOKEN_SEMI_COLON) {
			err = cc_token_stream_remove_head(stream, &token);
			assert(err == ESUCCESS);
			cc_token_delete(token);
			attributes->type = CC_NODE_ATTRIBUTE_DECLARATION;
			return cc_node_add_tail_child(parent, attributes);
		}
		if (err)
			return err;
	}

	/*
	 * DeclarationSpecifiers indicate linkage, storage-duration and part of the
	 * type of entities that the Declarators denote.
	 * The attributes specified here apply to each entity in the
	 * DeclaratorList.
	 */
	specifiers = declarator = NULL;
	err = parser_parse_declaration_specifiers(this, &specifiers);
	/* If attributes is non-NULL, then there must be at least one declarator */
	if (!err && attributes)
		err = ptrq_add_tail(&nodes, attributes);
	if (!err)
		err = ptrq_add_tail(&nodes, specifiers);
	if (!err)
		err = cc_token_stream_peek_head(stream, &token);
	if (err)
		return err;

	/* Declaration: DeclarationSpecifiers ; */
	if (attributes == NULL && cc_token_type(token) == CC_TOKEN_SEMI_COLON) {
		err = cc_token_stream_remove_head(stream, &token);
		assert(err == ESUCCESS);
		cc_token_delete(token);
		return parser_process_declaration(this, &nodes);
	}

	/* Parse a single Declarator first */
	err = parser_parse_declarator(this, &declarator);
	/* Should not be an AbstractDeclarator */
	if (!err)
		assert(cc_node_type(declarator) == CC_NODE_DECLARATOR);
	if (!err)
		err = ptrq_add_tail(&nodes, declarator);
	if (!err)
		err = cc_token_stream_peek_head(stream, &token);
	if (err)
		return err;
	if (cc_token_type(token) == CC_TOKEN_LEFT_BRACE)
		return parser_parse_function_definition(this, &nodes);
	return parser_parse_declaration(this, &nodes);
}
/*****************************************************************************/
static
err_t parser_parse_translation_unit(struct parser *this,
									struct cc_node **out)
{
	err_t err;

	/* TranslationUnit is the root of ast; array of ExternalDeclaration */
	out[0] = cc_node_new(CC_NODE_TRANSLATION_UNIT);
	if (out[0] == NULL)
		return ENOMEM;

	/* Parse array of ExternalDeclaration */
	while (true) {
		err = parser_parse_external_declaration(this, out[0]);
		if (err)
			break;
	}
	return err == EOF ? ESUCCESS : err;
}
/*****************************************************************************/
static
void cc_node_print(const struct cc_node *this)
{
	int i;
	const struct cc_node *child;

	/*
	 * Only identifiers, numbers, char-consts, and string-literals have
	 * non-null string field. The string is not nul terminated.
	 */
	printf("\n(");
	if (cc_node_is_identifier(this) && !cc_node_is_key_word(this))
		printf("%s", this->u.identifier->string);
	else if (cc_node_is_number(this))
		printf("%s", this->u.number->string);
	else if (cc_node_is_char_const(this))
		printf("%s", this->u.char_const->string);
	else if (cc_node_is_string_literal(this))
		printf("%s", this->u.string_literal->string);
	else
		printf("%s", &g_cc_node_type_str[this->type][strlen("CC_NODE_")]);
	CC_NODE_FOR_EACH_CHILD(this, i, child)
		cc_node_print(child);
	printf(")\n");
}

//static
void parser_print_ast(const struct parser *this)
{
	cc_node_print(this->root);
}
/*****************************************************************************/
err_t parser_parse(struct parser *this)
{
	err_t err;
	err = parser_parse_translation_unit(this, &this->root);
	if (!err)
		parser_cleanup0(this);
	assert(!err);
	return err;
}
