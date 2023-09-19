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

/* From src/cpp/lexer.c */
extern const char *g_key_words[];
extern const char *g_punctuators[];
static const char *g_cc_token_type_str[] = {
#define DEF(t)	"CC_TOKEN_" # t,
#include <inc/cpp/tokens.h>
#include <inc/cc/tokens.h>
#undef DEF
};
/*****************************************************************************/
static
err_t cc_storage_class_specifiers_add(cc_storage_class_specifiers_t *this,
									  const enum cc_token_type type)
{
	cc_storage_class_specifiers_t ts = *this;

	if (type == CC_TOKEN_THREAD_LOCAL) {
		/* Can't have more than one */
		if (bits_get(ts, CC_STORAGE_CLASS_SPECIFIER_THREAD_LOCAL))
			return EINVAL;
		/* thread_local can be used with static/extern */
		ts &= bits_off(CC_STORAGE_CLASS_SPECIFIER_STATIC);
		ts &= bits_off(CC_STORAGE_CLASS_SPECIFIER_EXTERN);
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_STORAGE_CLASS_SPECIFIER_THREAD_LOCAL);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_AUTO) {
		/* Can't have more than one */
		if (bits_get(ts, CC_STORAGE_CLASS_SPECIFIER_AUTO))
			return EINVAL;
		/* auto can be used with all except typedef */
		if (bits_get(ts, CC_STORAGE_CLASS_SPECIFIER_TYPE_DEF))
			return EINVAL;
		*this |= bits_on(CC_STORAGE_CLASS_SPECIFIER_AUTO);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_CONST_EXPR) {
		/* Can't have more than one */
		if (bits_get(ts, CC_STORAGE_CLASS_SPECIFIER_CONST_EXPR))
			return EINVAL;
		/* const-expr can be used with auto/register/static */
		ts &= bits_off(CC_STORAGE_CLASS_SPECIFIER_AUTO);
		ts &= bits_off(CC_STORAGE_CLASS_SPECIFIER_REGISTER);
		ts &= bits_off(CC_STORAGE_CLASS_SPECIFIER_STATIC);
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_STORAGE_CLASS_SPECIFIER_CONST_EXPR);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_TYPE_DEF) {
		/* Can't have more than one */
		if (bits_get(ts, CC_STORAGE_CLASS_SPECIFIER_TYPE_DEF))
			return EINVAL;
		/* type-def can't be used with anything else */
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_STORAGE_CLASS_SPECIFIER_TYPE_DEF);
		return ESUCCESS;
	}
	assert(0);	/* TODO */
	return EINVAL;
}

//static
err_t cc_type_qualifiers_add(cc_type_qualifiers_t *this,
							 const enum cc_token_type type)
{
	if (type == CC_TOKEN_CONST)
		*this |= bits_on(CC_TYPE_QUALIFIER_CONST);
	else if (type == CC_TOKEN_RESTRICT)
		*this |= bits_on(CC_TYPE_QUALIFIER_RESTRICT);
	else if (type == CC_TOKEN_VOLATILE)
		*this |= bits_on(CC_TYPE_QUALIFIER_VOLATILE);
	else if (type == CC_TOKEN_ATOMIC)
		*this |= bits_on(CC_TYPE_QUALIFIER_ATOMIC);
	else
		return EINVAL;
	return ESUCCESS;
}

static
err_t cc_type_specifiers_add(cc_type_specifiers_t *this,
							 const enum cc_token_type type)
{
	cc_type_specifiers_t ts = *this;

	if (type == CC_TOKEN_SIGNED ||
		type == CC_TOKEN_UNSIGNED) {
		/* Can't have the signedness more than once */
		if (bits_get(ts, CC_TYPE_SPECIFIER_SIGNED) ||
			bits_get(ts, CC_TYPE_SPECIFIER_UNSIGNED))
			return EINVAL;
		/* signed/unsigned can be used with char/short/int/long/bit-int */
		ts &= bits_off(CC_TYPE_SPECIFIER_CHAR);
		ts &= bits_off(CC_TYPE_SPECIFIER_SHORT);
		ts &= bits_off(CC_TYPE_SPECIFIER_INT);
		ts &= bits_off(CC_TYPE_SPECIFIER_LONG_0);
		ts &= bits_off(CC_TYPE_SPECIFIER_LONG_1);
		ts &= bits_off(CC_TYPE_SPECIFIER_BIT_INT);
		if (ts)
			return EINVAL;
		if (type == CC_TOKEN_SIGNED)
			*this |= bits_on(CC_TYPE_SPECIFIER_SIGNED);
		else
			*this |= bits_on(CC_TYPE_SPECIFIER_UNSIGNED);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_CHAR) {
		/* Can't have more than one chars */
		if (bits_get(ts, CC_TYPE_SPECIFIER_CHAR))
			return EINVAL;
		/* char can be used with signed/unsigned */
		ts &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		ts &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_TYPE_SPECIFIER_CHAR);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_SHORT) {
		/* Can't have more than one short */
		if (bits_get(ts, CC_TYPE_SPECIFIER_SHORT))
			return EINVAL;
		/* short can be used with int/signed/unsigned */
		ts &= bits_off(CC_TYPE_SPECIFIER_INT);
		ts &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		ts &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_TYPE_SPECIFIER_SHORT);
	}

	if (type == CC_TOKEN_INT) {
		/* Can't have more than one ints */
		if (bits_get(ts, CC_TYPE_SPECIFIER_INT))
			return EINVAL;
		/* int can be used with long/short/signed/unsigned */
		ts &= bits_off(CC_TYPE_SPECIFIER_LONG_0);
		ts &= bits_off(CC_TYPE_SPECIFIER_LONG_1);
		ts &= bits_off(CC_TYPE_SPECIFIER_SHORT);
		ts &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		ts &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_TYPE_SPECIFIER_INT);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_LONG) {
		/* Can't have more than 2 longs */
		if (bits_get(ts, CC_TYPE_SPECIFIER_LONG_0) &&
			bits_get(ts, CC_TYPE_SPECIFIER_LONG_1))
			return EINVAL;
		/* long can be used with int/long/signed/unsigned */
		ts &= bits_off(CC_TYPE_SPECIFIER_LONG_0);
		ts &= bits_off(CC_TYPE_SPECIFIER_LONG_1);
		ts &= bits_off(CC_TYPE_SPECIFIER_INT);
		ts &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		ts &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (ts)
			return EINVAL;
		ts = *this;
		if (!bits_get(ts, CC_TYPE_SPECIFIER_LONG_0))
			*this |= bits_on(CC_TYPE_SPECIFIER_LONG_0);
		else
			*this |= bits_on(CC_TYPE_SPECIFIER_LONG_1);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_BIT_INT) {
		/* Can't have more than one */
		if (bits_get(ts, CC_TYPE_SPECIFIER_BIT_INT))
			return EINVAL;
		/* bit-int can be used with signed/unsigned */
		ts &= bits_off(CC_TYPE_SPECIFIER_SIGNED);
		ts &= bits_off(CC_TYPE_SPECIFIER_UNSIGNED);
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_TYPE_SPECIFIER_BIT_INT);
	}

	if (type == CC_TOKEN_BOOL) {
		/* Can't have more than one */
		if (bits_get(ts, CC_TYPE_SPECIFIER_BOOL))
			return EINVAL;
		/* bool can't be used with others */
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_TYPE_SPECIFIER_BOOL);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_STRUCT) {
		/* Can't have more than one */
		if (bits_get(ts, CC_TYPE_SPECIFIER_STRUCT))
			return EINVAL;
		/* Can't be used with others */
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_TYPE_SPECIFIER_STRUCT);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_UNION) {
		/* Can't have more than one */
		if (bits_get(ts, CC_TYPE_SPECIFIER_UNION))
			return EINVAL;
		/* Can't be used with others */
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_TYPE_SPECIFIER_UNION);
		return ESUCCESS;
	}

	if (type == CC_TOKEN_ENUM) {
		/* Can't have more than one */
		if (bits_get(ts, CC_TYPE_SPECIFIER_ENUM))
			return EINVAL;
		/* Can't be used with others */
		if (ts)
			return EINVAL;
		*this |= bits_on(CC_TYPE_SPECIFIER_ENUM);
		return ESUCCESS;
	}
	assert(0);	/* TODO */
	return EINVAL;
}
/*****************************************************************************/
static
err_t parser_parse(struct parser *this,
				   const enum cc_token_type type,
				   struct cc_node *in[],
				   struct cc_node **out);
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
struct cc_node *cc_node_new(const enum cc_token_type type)
{
	struct cc_node *this = calloc(1, sizeof(*this));
	ptrt_init(&this->tree, cc_node_delete);
	this->type = type;
	return this;
}

/*****************************************************************************/
void cc_type_delete(void *p)
{
	struct cc_type *this = p;
	free(this);
	/* TODO: free based on cc_type_type */
}
/*****************************************************************************/
static
void cc_symtab_entry_delete(void *p)
{
	struct cc_symtab_entry *this = p;
	cc_node_delete(this->symbol);
	/* TODO */
	free(this);
}
/*****************************************************************************/
static
err_t cc_symtab_find_typedef(struct cc_symtab *this,
							 const char *name,
							 struct ptr_queue *out)	/* init by caller */
{
	int i, num_entries;
	err_t err;
	struct cc_symtab_entry *ste;
	struct cc_node *node;
	struct cc_node_identifier *ident;
	enum cc_name_space_type name_space = CC_NAME_SPACE_ORDINARY;
	/* type-def-names are in ordinary name-space */

	num_entries = 0;
	while (this) {
		PTRQ_FOR_EACH(&this->entries[name_space], i, ste) {
			if (cc_symtab_entry_type(ste) != CC_SYMTAB_ENTRY_TYPE_DEF)
				continue;
			node = ste->symbol;
			assert(cc_node_type(node) == CC_TOKEN_IDENTIFIER);
			ident = &node->u.identifier;
			assert(ident->string);
			if (strcmp(ident->string, name))
				continue;
			err = ptrq_add_tail(out, ste);
			if (err)
				return err;
			++num_entries;
		}
		this = ptrt_parent(&this->tree);
	}
	if (num_entries == 0)
		return ENOENT;
	return ESUCCESS;
}

static
void cc_symtab_delete(void *p)
{
	int i;
	struct cc_symtab *this = p;
	for (i = 0; i < CC_NAME_SPACE_MAX; ++i)
		ptrq_empty(&this->entries[i]);
	ptrt_empty(&this->tree);
	free(this);
}

static inline
struct cc_symtab *cc_symtab_new(const enum cc_symtab_scope scope)
{
	int i;
	struct cc_symtab *this;

	this = malloc(sizeof(*this));
	if (this == NULL)
		return NULL;
	this->scope = scope;
	ptrt_init(&this->tree, cc_symtab_delete);
	for (i = 0; i < CC_NAME_SPACE_MAX; ++i)
		ptrq_init(&this->entries[i], cc_symtab_entry_delete);
	return this;
}
/*****************************************************************************/
err_t parser_build_types(struct parser *this)
{
	err_t err;
	int i;
	char *str;
	struct cc_node	*node;
	struct cc_node_identifier	*ident;
	struct cc_symtab_entry *ste;
	static const enum cc_token_type types[] = {
		CC_TOKEN_BOOL,
		CC_TOKEN_CHAR,
		CC_TOKEN_SHORT,
		CC_TOKEN_INT,
		CC_TOKEN_LONG,
		CC_TOKEN_IDENTIFIER,	/* 'long long' isn't a keyword. */
	};
	static const struct cc_symtab_entry_integer ints[] = {
		{8, 1, 7, 8, false},	/* bool */
		{8, 7, 0, 8, true},		/* char */
		{16, 15, 0, 16, true},	/* short */
		{32, 31, 0, 32, true},	/* int */
		{64, 63, 0, 64, true},	/* long */
		{64, 63, 0, 64, true},	/* long long */
	};

	/*
	 * The type for char will have a child with the name 'signed' since, char
	 * and signed-char are different, incompatible types. Other int types are
	 * all 'signed', hence they do not need an extra 'signed' child.
	 */
	for (i = 0; i < (int)ARRAY_SIZE(types); ++i) {
		node = cc_node_new(types[i]);
		ste = malloc(sizeof(*ste));
		if (ste == NULL || node == NULL)
			return ENOMEM;
		ident = &node->u.identifier;
		ident->type = ste;	/* Points to self */
		if (cc_node_type(node) == CC_TOKEN_IDENTIFIER) {
			/* 'long long' isn't a keyword */
			ident->string = str = malloc(strlen("long long") + 1);
			if (str == NULL)
				return ENOMEM;
			strcpy(str, "long long");
		}
		/* current scope should be FILE */
		assert(cc_symtab_scope(this->symbols) == CC_SYMTAB_SCOPE_FILE);
		ste->symbols = this->symbols;
		ste->symbol = node;
		ste->prev = NULL;	/* No prev decls exist. */
		ste->type = CC_SYMTAB_ENTRY_INTEGER;	/* all these are int types */
		ste->linkage = CC_LINKAGE_NONE;		/* types have no linkages */
		ste->storage = CC_TOKEN_INVALID;	/* types have no storage durn */
		ste->name_space = CC_NAME_SPACE_ORDINARY;
		memcpy(&ste->u.integer, &ints[i], sizeof(ints[i]));
		err = cc_symtab_add_entry(this->symbols, ste);
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
	this->symbols = cc_symtab_new(CC_SYMTAB_SCOPE_FILE);
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
	cc_node_delete(&this->root);
	cc_symtab_delete(&this->symbols);
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

static
err_t cc_token_stream_read_token(struct cc_token_stream *this,
								 struct cc_token **out)
{
	struct cc_token *token;
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

	/* key-words and punctuators do not have a src-len. */
#if 0
	if (cc_token_type_is_key_word(type)) {
		token->string = g_key_words[type - CC_TOKEN_ATOMIC];
		token->string_len = strlen(token->string);
		goto done;
	}
	if (cc_token_type_is_punctuator(type)) {
		token->string = g_punctuators[type - CC_TOKEN_LEFT_BRACE];
		token->string_len = strlen(token->string);
		goto done;
	}
#else
	if (cc_token_type_is_key_word(type) ||
		cc_token_type_is_punctuator(type)) {
		token->string = NULL;
		token->string_len = 0;
		goto done;
	}
#endif
	memcpy(&src_len, &this->buffer[position], sizeof(src_len));
	position += sizeof(src_len);
	src = NULL;
	if (src_len) {
		src = malloc(src_len + 1);
		if (src == NULL)
			return ENOMEM;
		src[src_len] = 0;
		memcpy(src, &this->buffer[position], src_len);
		position += src_len;
	}
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
err_t parser_parse_translation_unit(struct parser *this,
									struct cc_node **out)
{
	err_t err;
	enum cc_token_type type;
	struct cc_node *root;

	/* TranslationUnit is the root of ast; array of ExternalDeclaration */
	root = cc_node_new(CC_TOKEN_TRANSLATION_UNIT);
	if (root == NULL)
		return ENOMEM;

	/* Parse array of ExternalDeclaration */
	type = CC_TOKEN_EXTERNAL_DECLARATION;
	while (true) {
		err = parser_parse(this, type, &root, NULL);
		if (err)
			break;
	}
	err = err == EOF ? ESUCCESS : err;
	if (!err)
		*out = root;
	return err;
}
/*****************************************************************************/
static
err_t parser_parse_external_declaration(struct parser *this,
										struct cc_node *in[])
{
	err_t err;
	enum cc_token_type type;
	struct cc_node *parent;
	struct cc_token_stream *stream;
	struct cc_token *token;
	struct cc_node *attributes;
	struct cc_node *specifiers;
	struct cc_node *declarator;
	struct cc_node *definition;
	struct cc_node *declaration;
	struct cc_node *nodes[5];

	parent = in[0];
	assert(parent);
	assert(cc_node_type(parent) == CC_TOKEN_TRANSLATION_UNIT);

	/* Is it a static_assert declaration? */
	stream = &this->stream;
	err = cc_token_stream_peek_head(stream, &token);
	if (err)
		return err;
	if (cc_token_type(token) == CC_TOKEN_STATIC_ASSERT) {
		/*
		 * Since we want to store the StaticAssertDeclarationn within
		 * TranslationUnit, pass TranslationUnit as the parent.
		 */
		type = CC_TOKEN_STATIC_ASSERT_DECLARATION;
		return parser_parse(this, type, in, NULL);
	}

	/* Is it an AttributeDeclaration? */
	attributes = NULL;
	if (cc_token_type(token) == CC_TOKEN_LEFT_BRACKET) {
		type = CC_TOKEN_ATTRIBUTE_SPECIFIER_SEQUENCE;
		err = parser_parse(this, type, NULL, &attributes);
		if (!err)
			err = cc_token_stream_peek_head(stream, &token);
		if (!err && cc_token_type(token) == CC_TOKEN_SEMI_COLON) {
			err = cc_token_stream_remove_head(stream, &token);
			assert(err == ESUCCESS);
			cc_token_delete(token);
			/* The type is still AttributeSpecifierSequence. Change it. */
			attributes->type = CC_TOKEN_ATTRIBUTE_DECLARATION;
			return cc_node_add_tail_child(parent, attributes);
		}
		if (err)
			return err;
		assert(attributes);
	}

	/*
	 * DeclarationSpecifiers indicate linkage, storage-duration and part of the
	 * type of entities that the Declarators denote.
	 */
	specifiers = declarator = definition = declaration = NULL;
	type = CC_TOKEN_DECLARATION_SPECIFIERS;
	err = parser_parse(this, type, NULL, &specifiers);
	if (err)
		return err;

	/* If attributes is non-NULL, then there must be at least one declarator */
	nodes[0] = parent;		/* TranslationUnit */
	nodes[1] = attributes;	/* may be null */
	nodes[2] = specifiers;
	nodes[3] = NULL;
	err = cc_token_stream_peek_head(stream, &token);
	if (err)
		return err;

	/* DeclarationSpecifiers ; */
	type = CC_TOKEN_DECLARATION;
	if (attributes == NULL && cc_token_type(token) == CC_TOKEN_SEMI_COLON)
		return parser_parse(this, type, nodes, NULL);

	/* Parse a single Declarator first */
	type = CC_TOKEN_DECLARATOR;
	err = parser_parse(this, type, NULL, &declarator);
	if (err)
		return err;
	nodes[3] = declarator;
	nodes[4] = NULL;
	err = cc_token_stream_peek_head(stream, &token);
	if (err)
		return err;
	type = CC_TOKEN_DECLARATION;
	if (cc_token_type(token) == CC_TOKEN_LEFT_BRACE)
		type = CC_TOKEN_FUNCTION_DEFINITION;
	/*
	 * We want the parser to store the Declaration/FunctionDefinition directly
	 * into the parent (TranslationUnit). So no need for out.
	 */
	return parser_parse(this, type, nodes, NULL);
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
	enum cc_token_type type;
	const struct cc_symtab_entry *ste;
	struct cc_node *specifiers;
	struct cc_token_stream *stream;
	struct cc_token *token;

	/* No parent. Create ourselves and return in *out */

	/*
	 * array of DeclarationSpecifier elements, each element optionally followed
	 * by AttributeSpecifierSequence.
	 */
	ptrq_init(&stes, NULL);
	stream = parser_token_stream(this);
	specifiers = cc_node_new(CC_TOKEN_DECLARATION_SPECIFIERS);
	if (specifiers == NULL)
		return ENOMEM;
	token = NULL;
	while (true) {
		/* We do not expect an error, not even an EOF */
		err = cc_token_stream_peek_head(stream, &token);
		if (err)
			return err;

		is_specifier = false;
		if (cc_token_is_type_specifier(token) ||
			cc_token_is_type_qualifier(token) ||
			cc_token_is_alignment_specifier(token) ||
			cc_token_is_storage_class_specifier(token) ||
			cc_token_is_function_specifier(token))
			is_specifier = true;
		if (!is_specifier)
			break;

		/* Determine if this (non-keyword) Identifier is a TypedefName */
		if (cc_token_type(token) == CC_TOKEN_IDENTIFIER) {
			/* Should be a TypedefName. If not, break */
			str = cc_token_string(token);
			err = cc_symtab_find_typedef(this->symbols, str, &stes);
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

		type = CC_TOKEN_DECLARATION_SPECIFIER;
		err = parser_parse(this, type, &specifiers, NULL);
		if (err)
			return err;

		/* Does an AttributeSpecifierSequence follow? */
		err = cc_token_stream_peek_head(stream, &token);
		if (err || cc_token_type(token) != CC_TOKEN_LEFT_BRACKET)
			continue;	/* no attrs follow */
		/* TODO: Move this to parser_parse_declaration_specifier. */
		assert(0);
		type = CC_TOKEN_ATTRIBUTE_SPECIFIER_SEQUENCE;
		err = parser_parse(this, type, &specifiers, NULL);
		if (err)
			return err;
	}
	assert(err == ESUCCESS);
	*out = specifiers;
	return err;
}

static
err_t parser_parse_type_specifier_atomic(struct parser *this,
										 struct cc_node *in[])
{
	assert(0);
	(void)this;
	(void)in;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier_bit_int(struct parser *this,
										  struct cc_node *in[])
{
	assert(0);
	(void)this;
	(void)in;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier_struct(struct parser *this,
										 struct cc_node *in[])
{
	assert(0);
	(void)this;
	(void)in;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier_enum(struct parser *this,
									   struct cc_node *in[])
{
	assert(0);
	(void)this;
	(void)in;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier_type_of(struct parser *this,
										  struct cc_node *in[])
{
	assert(0);
	(void)this;
	(void)in;
	return ENOTSUP;
}

static
err_t parser_parse_type_specifier(struct parser *this,
								  struct cc_node *in[])
{
	err_t err;
	int i;
	enum cc_token_type type;
	struct cc_node *parent, *child, *specifiers;
	struct cc_node_type_specifiers *ts;
	struct cc_token_stream *stream;
	struct cc_token *token;

	stream = parser_token_stream(this);
	parent = in[0];
	assert(parent);
	/* For now */
	assert(cc_node_type(parent) == CC_TOKEN_DECLARATION_SPECIFIERS);

	err = cc_token_stream_peek_head(stream, &token);
	assert(err == ESUCCESS);
	type = cc_token_type(token);

	/* Search the parent for a type-specifier node */
	specifiers = NULL;
	CC_NODE_FOR_EACH_CHILD(parent, i, child) {
		if (cc_node_type(child) != CC_TOKEN_TYPE_SPECIFIERS)
			continue;
		specifiers = child;
		break;
	}
	if (specifiers == NULL) {
		specifiers = cc_node_new(CC_TOKEN_TYPE_SPECIFIERS);
		if (specifiers == NULL)
			return ENOMEM;
		err = cc_node_add_tail_child(parent, specifiers);
		if (err)
			return err;
	}

	/*
	 * Attempt inserting into the bit-mask. If that fails, then this must be a
	 * syntax error.
	 */
	ts = &specifiers->u.type_specifiers;
	err = cc_type_specifiers_add(&ts->specifiers, type);
	if (err)
		return err;

	/*
	 * We do not set ts.type here, as we are still in the middle of parsing
	 * a declaration, etc. parent constructs.
	 * The job of locating or creating the type lies with the declaration etc
	 * that will process the specifiers as a whole.
	 */

	if (type == CC_TOKEN_ATOMIC)
		return parser_parse_type_specifier_atomic(this, in);
	if (type == CC_TOKEN_BIT_INT)
		return parser_parse_type_specifier_bit_int(this, in);
	if (type == CC_TOKEN_ENUM)
		return parser_parse_type_specifier_enum(this, in);
	if (type == CC_TOKEN_STRUCT ||
		type == CC_TOKEN_UNION)
		return parser_parse_type_specifier_struct(this, in);
	if (type == CC_TOKEN_TYPE_OF ||
		type == CC_TOKEN_TYPE_OF_UNQUAL)
		return parser_parse_type_specifier_type_of(this, in);
	/* Else, single-token specifiers */
	err = cc_token_stream_remove_head(stream, &token);
	assert(err == ESUCCESS);
	cc_token_delete(token);
	return err;
}

static
err_t parser_parse_type_qualifier(struct parser *this,
								  struct cc_node *in[])
{
	assert(0);
	(void)this;
	(void)in;
	return ENOTSUP;
}

static
err_t parser_parse_alignment_specifier(struct parser *this,
									   struct cc_node *in[])
{
	assert(0);
	(void)this;
	(void)in;
	return ENOTSUP;
}

static
err_t parser_parse_storage_class_specifier(struct parser *this,
										   struct cc_node *in[])
{
	err_t err;
	int i;
	enum cc_token_type type;
	struct cc_node *parent, *child, *specifiers;
	struct cc_node_storage_class_specifiers *scs;
	struct cc_token_stream *stream;
	struct cc_token *token;

	stream = parser_token_stream(this);
	parent = in[0];
	assert(parent);
	/* For now */
	assert(cc_node_type(parent) == CC_TOKEN_DECLARATION_SPECIFIERS);

	err = cc_token_stream_remove_head(stream, &token);
	assert(err == ESUCCESS);
	type = cc_token_type(token);
	cc_token_delete(token);

	/* Search the parent for a storage-class-specifier node */
	specifiers = NULL;
	CC_NODE_FOR_EACH_CHILD(parent, i, child) {
		if (cc_node_type(child) != CC_TOKEN_STORAGE_CLASS_SPECIFIERS)
			continue;
		specifiers = child;
		break;
	}
	if (specifiers == NULL) {
		specifiers = cc_node_new(CC_TOKEN_STORAGE_CLASS_SPECIFIERS);
		if (specifiers == NULL)
			return ENOMEM;
		err = cc_node_add_tail_child(parent, specifiers);
		if (err)
			return err;
	}

	/*
	 * Attempt inserting into the bit-mask. If that fails, then this must be a
	 * syntax error.
	 */
	scs = &specifiers->u.storage_class_specifiers;
	return cc_storage_class_specifiers_add(&scs->specifiers, type);
}

static
err_t parser_parse_function_specifier(struct parser *this,
									  struct cc_node *in[])
{
	assert(0);
	(void)this;
	(void)in;
	return ENOTSUP;
}

static
err_t parser_parse_declaration_specifier(struct parser *this,
										 struct cc_node *in[])
{
	err_t err;
	struct cc_node *parent;
	struct cc_token_stream *stream;
	struct cc_token *token, *t;

	stream = parser_token_stream(this);
	parent = in[0];
	assert(parent);
	assert(cc_node_type(parent) == CC_TOKEN_DECLARATION_SPECIFIERS);

	err = cc_token_stream_peek_head(stream, &token);
	assert(err == ESUCCESS);

	/* Atomic may represent either a TypeSpecifier or a TypeQualifier */
	if (cc_token_type(token) == CC_TOKEN_ATOMIC) {
		err = cc_token_stream_peek_entry(stream, 1, &t);
		if (!err && cc_token_type(t) == CC_TOKEN_LEFT_PAREN)
			return parser_parse_type_specifier(this, in);
	}

	/* TypeQualifier check must precede TypeSpecifier due to _Atomic */
	if (cc_token_is_type_qualifier(token))
		return parser_parse_type_qualifier(this, in);
	if (cc_token_is_type_specifier(token))
		return parser_parse_type_specifier(this, in);
	if (cc_token_is_alignment_specifier(token))
		return parser_parse_alignment_specifier(this, in);
	if (cc_token_is_storage_class_specifier(token))
		return parser_parse_storage_class_specifier(this, in);
	if (cc_token_is_function_specifier(token))
		return parser_parse_function_specifier(this, in);

	/*
	 * This function knows the exact type of the DeclarationSpecifier.
	 * Scan the AttributeSpecifierSequence that can follow a
	 * DeclarationSpecifier here.
	 */
	return EINVAL;
}
/*****************************************************************************/
static
err_t parser_parse_declarator(struct parser *this,
							  struct cc_node **out)
{
	err_t err;
	bool identifier_moved;
	struct val_queue stack;
	struct ptr_queue qualifiers;
	struct ptr_queue list;	/* output type-list. cc_symtab_entry * */
	struct cc_symtab_entry *list;	/* the output type-list */
	struct cc_symtab_entry *object;
	enum cc_token_type type;
	struct cc_token_stream *stream;
	struct cc_token *token;
	struct cc_node *pointer, *declarator;

	stream = parser_token_stream(this);
	*out = declarator = cc_node_new(CC_TOKEN_DECLARATOR);
	if (declarator == NULL)
		return ENOMEM;

	/* The operand-stack need only store cc_token_type enums */
	valq_init(&stack, sizeof(enum cc_token_type), NULL);

	/*
	 * We do not pass a destructor since the elements will anyways move
	 * into a real symtable.
	 */
	ptrq_init(&list, NULL);
	identifier_moved = false;

	object = NULL;
	while (true) {
		err = cc_token_stream_peek_head(stream, &token);
		if (err)
			return err;
		type = cc_token_type(token);

		/* TODO: We do not expect key-words here */
		if (type == CC_TOKEN_IDENTIFIER) {
			/* Can't have two idents */
			if (identifier_moved)
				return EINVAL;
			identifier_moved = true;
			err = cc_token_stream_remove_head(stream, &token);
			assert(err == ESUCCESS);
			object = calloc(1, sizeof(*object));
			node = cc_node_new(CC_TOKEN_IDENTIFIER);
			if (object == NULL || node == NULL)
				return ENOMEM;

			node->string = cc_token_string(token);
			node->string_len = cc_token_string_length(token);
			token->string = NULL;
			cc_token_delete(token);

			object->symbol = node;
			object->type = CC_SYMTAB_ENTRY_OBJECT;
			err = ptrq_add_tail(&list, object);
			if (err)
				return err;
			continue;
		}

		/* If the stack is empty and we do not see a ( or [, then done */
		if (valq_is_empty(&stack) &&
			type != CC_TOKEN_LEFT_PAREN &&
			type != CC_TOKEN_LEFT_BRACKET)
			break;

		if (type == CC_TOKEN_MUL) {
			/* Can't have a Pointer after identifier is parsed. */
			if (identifier_moved)
				return EINVAL;
			err = cc_token_stream_remove_head(stream, &token);
			assert(err == ESUCCESS);
			cc_token_delete(token);

			object = calloc(1, sizeof(*object));
			if (object == NULL)
				return ENOMEM;
			object->type = CC_SYMTAB_ENTRY_POINTER;
			err = parser_parse_type_qualifiers(this,
	}
	if (cc_token_type(token) == CC_TOKEN_MUL) {
		type = CC_TOKEN_POINTER;
		err = parser_parse(this, type, NULL, &pointer);
		if (err)
			return err;
	}
	/* Parse the declarator and create a list */
	(void)out;
	return ENOTSUP;
}
/*****************************************************************************/
static
err_t parser_parse(struct parser *this,
				   const enum cc_token_type type,
				   struct cc_node *in[],
				   struct cc_node **out)
{
	err_t err;

	switch (type) {
	case CC_TOKEN_TRANSLATION_UNIT:
		assert(in == NULL);
		err = parser_parse_translation_unit(this, out);
		break;
	case CC_TOKEN_EXTERNAL_DECLARATION:
		/* in[0] Must be the parent (TranslationUnit) */
		assert(in);

		/* Since the parent is passed in in[], no need to pass out */
		assert(out == NULL);
		err = parser_parse_external_declaration(this, in);
		break;
	case CC_TOKEN_DECLARATION_SPECIFIERS:
		/* No need for a parent, but we do need out to be non-NULL */
		assert(in == NULL);
		assert(out);
		err = parser_parse_declaration_specifiers(this, out);
		break;
	case CC_TOKEN_DECLARATION_SPECIFIER:
		assert(in);
		assert(out == NULL);
		err = parser_parse_declaration_specifier(this, in);
		break;
	case CC_TOKEN_DECLARATOR:
		assert(in == NULL);
		assert(out);
		err = parser_parse_declarator(this, out);
		break;
	default:
		printf("%s: %s unsupported\n", __func__,
			   g_cc_token_type_str[type]);
		err = EINVAL;
		exit(err);
		break;
	}
	return err;
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
		printf("%s", this->u.identifier.string);
	else if (cc_node_is_number(this))
		printf("%s", this->u.number.string);
	else if (cc_node_is_char_const(this))
		printf("%s", this->u.char_const.string);
	else if (cc_node_is_string_literal(this))
		printf("%s", this->u.string.string);
	else
		printf("%s", &g_cc_token_type_str[this->type][strlen("CC_TOKEN_")]);
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
err_t parser_parse_tokens(struct parser *this)
{
	err_t err;
	enum cc_token_type type;

	type = CC_TOKEN_TRANSLATION_UNIT;
	err = parser_parse(this, type, NULL, &this->root);
	if (!err)
		parser_cleanup0(this);
#if 0
	if (!err)
		parser_print_parse_tree(this);
	if (false && !err)
		err = parser_visit_parse_tree(this);
#endif
	assert(!err);
	return err;
}
