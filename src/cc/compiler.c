/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include "compiler.h"

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
/*****************************************************************************/
/*
 * If *out is non-NULL, then it is the parent of the construct being currently
 * parsed. If *out is NULL, then the construct being parsed must create itself
 * and return it.
 */
static
err_t compiler_parse(struct compiler *this,
					 const enum cc_token_type type,
					 struct cc_node **out);
/*****************************************************************************/
static const char *g_cc_token_type_str[] = {
#define DEF(t)	"CC_TOKEN_" # t,
#include <inc/cpp/tokens.h>
#include <inc/cc/tokens.h>
#undef DEF
};

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
	struct cc_node *this = p;
	free((void *)this->string);
	ptrt_empty(&this->tree);
	free(this);
}
/*****************************************************************************/
void cc_type_delete(void *p)
{
	assert(0);	/* TODO */
	struct cc_type *this = p;
	free(this);
}
/*****************************************************************************/
static
void cc_symbol_table_entry_delete(void *p)
{
	struct cc_symbol_table_entry *this = p;
	cc_token_delete(this->identifier);
	if (cc_symbol_table_entry_type(this) == CC_SYMBOL_TABLE_ENTRY_TYPE)
		cc_type_delete(this->u.type);
	free(this);
}
/*****************************************************************************/
static
void cc_symbol_table_delete(void *p)
{
	struct cc_symbol_table *this = p;
	ptrq_empty(&this->entries);
	ptrt_empty(&this->tree);
	free(this);
}
/*****************************************************************************/
err_t compiler_build_types(struct compiler *this)
{
	err_t err;
	int i;
	struct cc_token	*identifier;
	struct cc_symbol_table_entry *ste;
	struct cc_type *type;
	static const enum cc_type_type codes[] = {	/* same order as below */
		CC_TYPE_BOOL,
		CC_TYPE_CHAR,
		CC_TYPE_SHORT,
		CC_TYPE_INT,
		CC_TYPE_LONG,
		CC_TYPE_LONG_LONG,
	};
	static const struct cc_type_integer types[] = {	/* unqualified types. */
		{8, 1, 7, 8, false},	/* bool */
		{8, 7, 0, 8, true},		/* char */
		{16, 15, 0, 16, true},	/* short */
		{32, 31, 0, 32, true},	/* int */
		{64, 63, 0, 64, true},	/* long */
		{64, 63, 0, 64, true},	/* long long */
	};

	/*
	 * The type for char will have a child with the name 'signed' since, char
	 * and signed-char are different. Other int types are all 'signed', hence
	 * they do not need an extra 'signed' child.
	 */
	for (i = 0; i < (int)ARRAY_SIZE(types); ++i) {
		ste = malloc(sizeof(*ste));
		identifier = malloc(sizeof(*identifier));
		type = malloc(sizeof(*type));
		if (type == NULL || ste == NULL || identifier == NULL)
			return ENOMEM;
		cc_token_init(identifier);
		identifier->type = CC_TOKEN_BOOL;
		type->type = codes[i];
		memcpy(&type->u.integer, &types[i], sizeof(types[i]));
		ste->identifier = identifier;
		ste->type = CC_SYMBOL_TABLE_ENTRY_TYPE;
		ste->u.type = type;
		err = cc_symbol_table_add_entry(&this->symbols, ste);
		if (err)
			return err;
	}
	return err;
}

err_t compiler_new(const char *path,
				   struct compiler **out)
{
	err_t err;
	int fd, ret;
	const char *buffer;
	size_t size;
	struct compiler *this;
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

	cc_node_init(&this->root);
	cc_symbol_table_init(&this->symbols);
	this->root.type = CC_TOKEN_TRANSLATION_UNIT;
	err = compiler_build_types(this);
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
void compiler_cleanup0(struct compiler *this)
{
	assert(this);
	munmap((void *)this->stream.buffer, this->stream.buffer_size);
	close(this->cpp_tokens_fd);
	unlink(this->cpp_tokens_path);
	free((void *)this->cpp_tokens_path);
	cc_token_stream_empty(&this->stream);
}

err_t compiler_delete(struct compiler *this)
{
	assert(this);
	cc_node_delete(&this->root);
	cc_symbol_table_delete(&this->symbols);
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
err_t compiler_parse_translation_unit(struct compiler *this,
									  struct cc_node **out)
{
	err_t err;
	struct cc_node *root;

	/* TranslationUnit is an array of ExternalDeclaration */
	assert(*out == NULL);
	root = malloc(sizeof(*root));
	if (root == NULL)
		return ENOMEM;
	cc_node_init(root);
	root->type = CC_TOKEN_TRANSLATION_UNIT;
	while (true) {
		err = compiler_parse(this, CC_TOKEN_EXTERNAL_DECLARATION, &root);
		if (err)
			break;
	}
	err = err == EOF ? ESUCCESS : err;
	if (!err)
		*out = root;
	return err;
}

static
err_t compiler_parse_external_declaration(struct compiler *this,
										  struct cc_node **out)
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

	parent = *out;
	assert(parent);
	assert(cc_node_type(parent) == CC_TOKEN_TRANSLATION_UNIT);

	/* Is it a static_assert declaration? */
	attributes = NULL;
	stream = &this->stream;
	err = cc_token_stream_peek_head(stream, &token);
	if (err)
		return err;

	if (cc_token_type(token) == CC_TOKEN_STATIC_ASSERT) {
		type = CC_TOKEN_STATIC_ASSERT_DECLARATION;
		return compiler_parse(this, type, out);
	}

	/* Is it an AttributeDeclaration? */
	if (cc_token_type(token) == CC_TOKEN_LEFT_BRACKET) {
		type = CC_TOKEN_ATTRIBUTE_SPECIFIER_SEQUENCE;
		err = compiler_parse(this, type, &attributes);
		if (!err)
			err = cc_token_stream_peek_head(stream, &token);
		if (!err && cc_token_type(token) == CC_TOKEN_SEMI_COLON) {
			err = cc_token_stream_remove_head(stream, &token);
			assert(err == ESUCCESS);
			cc_token_delete(token);
			attributes->type = CC_TOKEN_ATTRIBUTE_DECLARATION;
			return cc_node_add_tail_child(parent, attributes);
		}
		if (err)
			return err;
		assert(attributes);
	}

	/* Is it a FunctionDefinition. */
	err = compiler_parse(this, CC_TOKEN_DECLARATION_SPECIFIERS, &specifiers);
	if (!err)
		err = compiler_parse(this, CC_TOKEN_DECLARATOR, &declarator);
	if (!err)
		err = cc_token_stream_peek_head(stream, &token);
	if (!err && cc_token_type(token) == CC_TOKEN_LEFT_BRACE) {
		definition = malloc(sizeof(*definition));
		if (definition == NULL)
			return ENOMEM;
		cc_node_init(definition);
		definition->type = CC_TOKEN_FUNCTION_DEFINITION;
		if (attributes)
			err = cc_node_add_tail_child(definition, attributes);
		if (!err)
			err = cc_node_add_tail_child(definition, specifiers);
		if (!err)
			err = cc_node_add_tail_child(definition, declarator);
	}
	return err;
}

static
err_t compiler_parse(struct compiler *this,
					 const enum cc_token_type type,
					 struct cc_node **out)
{
	err_t err;

	switch (type) {
	case CC_TOKEN_TRANSLATION_UNIT:
		err = compiler_parse_translation_unit(this, out);
		break;
	case CC_TOKEN_EXTERNAL_DECLARATION:
		err = compiler_parse_external_declaration(this, out);
		break;
	default:
		err = EINVAL;
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
	 * string.
	 */
	printf("\n(");
	if (cc_node_is_number(this) ||
		cc_node_is_identifier(this) ||
		cc_node_is_char_const(this) ||
		cc_node_is_string_literal(this)) {
		assert(this->string);
		for (i = 0; i < (int)this->string_len; ++i)
			printf("%c", this->string[i]);
	} else {
		printf("%s", &g_cc_token_type_str[this->type][strlen("CC_TOKEN_")]);
	}
	PTRQ_FOR_EACH(&this->child_nodes, i, child)
		cc_node_print(child);
	printf(")\n");
}

//static
void compiler_print_ast(const struct compiler *this)
{
	cc_node_print(this->root);
}
/*****************************************************************************/
err_t compiler_compile(struct compiler *this)
{
	err_t err;
	err = compiler_parse(this, CC_TOKEN_TRANSLATION_UNIT, &this->root);
	if (!err)
		compiler_cleanup0(this);
#if 0
	if (!err)
		compiler_print_parse_tree(this);
	if (false && !err)
		err = compiler_visit_parse_tree(this);
#endif
	assert(!err);
	return err;
}
