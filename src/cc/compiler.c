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
struct cc_storage_class_specifiers {
	int	auto_specifier;
	int	constexpr_specifier;
	int	extern_specifier;
	int	register_specifier;
	int	static_specifier;
	int	thread_local_specifier;
	int	typedef_specifier;
};

static inline
void cc_storage_class_specifiers_incr(struct cc_storage_class_specifiers *this,
									  const enum cc_token_type type)
{
	switch (type) {
	case CC_TOKEN_AUTO:			++this->auto_specifier; break;
	case CC_TOKEN_CONST_EXPR:	++this->constexpr_specifier; break;
	case CC_TOKEN_EXTERN:		++this->extern_specifier; break;
	case CC_TOKEN_REGISTER:		++this->register_specifier; break;
	case CC_TOKEN_STATIC:		++this->static_specifier; break;
	case CC_TOKEN_THREAD_LOCAL:	++this->thread_local_specifier; break;
	case CC_TOKEN_TYPE_DEF:		++this->typedef_specifier; break;
	default:	assert(0); break;
	}
}
/*****************************************************************************/
struct cc_function_specifiers {
	int	inline_specifier;
	int	noreturn_specifier;
};

static inline
void cc_function_specifiers_incr(struct cc_function_specifiers *this,
								 const enum cc_token_type type)
{
	switch (type) {
	case CC_TOKEN_INLINE:		++this->inline_specifier; break;
	case CC_TOKEN_NO_RETURN:	++this->noreturn_specifier; break;
	default:	assert(0); break;
	}
}
/*****************************************************************************/
struct cc_type_qualifiers {
	int const_qualifier;
	int	restrict_qualifier;
	int	volatile_qualifier;
	int	atomic_qualifier;
};

static inline
void cc_type_qualifiers_incr(struct cc_type_qualifiers *this,
							 const enum cc_token_type type)
{
	switch (type) {
	case CC_TOKEN_CONST:	++this->const_qualifier; break;
	case CC_TOKEN_RESTRICT:	++this->restrict_qualifier; break;
	case CC_TOKEN_VOLATILE:	++this->volatile_qualifier; break;
	case CC_TOKEN_ATOMIC:	++this->atomic_qualifier; break;
	default:	assert(0); break;
	}
}
/*****************************************************************************/
struct cc_type_specifiers {
	int	void_specifier;
	int	char_specifier;
	int	short_specifier;
	int	int_specifier;
	int	long_specifier;
	int	float_specifier;
	int	double_specifier;
	int	signed_specifier;
	int	unsigned_specifier;
	int	bit_int_specifier;
	int	bool_specifier;
	int	complex_specifier;
	int	decimal_32_specifier;
	int	decimal_64_specifier;
	int	decimal_128_specifier;
	int	atomic_specifier;
	int	struct_specifier;
	int	union_specifier;
	int	enum_specifier;
	int	typedef_name_specifier;
	int	typeof_specifier;
	int	typeof_unqual_specifier;
	int	alignas_specifier;
};

static inline
void cc_type_specifiers_incr(struct cc_type_specifiers *this,
							 const enum cc_token_type type)
{
	switch (type) {
	case CC_TOKEN_VOID:				++this->void_specifier; break;
	case CC_TOKEN_CHAR:				++this->char_specifier; break;
	case CC_TOKEN_SHORT:			++this->short_specifier; break;
	case CC_TOKEN_INT:				++this->int_specifier; break;
	case CC_TOKEN_LONG:				++this->long_specifier; break;
	case CC_TOKEN_FLOAT:			++this->float_specifier; break;
	case CC_TOKEN_DOUBLE:			++this->double_specifier; break;
	case CC_TOKEN_SIGNED:			++this->signed_specifier; break;
	case CC_TOKEN_UNSIGNED:			++this->unsigned_specifier; break;
	case CC_TOKEN_BIT_INT:			++this->bit_int_specifier; break;
	case CC_TOKEN_BOOL:				++this->bool_specifier; break;
	case CC_TOKEN_COMPLEX:			++this->complex_specifier; break;
	case CC_TOKEN_DECIMAL_32:		++this->decimal_32_specifier; break;
	case CC_TOKEN_DECIMAL_64:		++this->decimal_64_specifier; break;
	case CC_TOKEN_DECIMAL_128:		++this->decimal_128_specifier; break;
	case CC_TOKEN_ATOMIC:			++this->atomic_specifier; break;
	case CC_TOKEN_STRUCT:			++this->struct_specifier; break;
	case CC_TOKEN_UNION:			++this->union_specifier; break;
	case CC_TOKEN_ENUM:				++this->enum_specifier; break;
	case CC_TOKEN_IDENTIFIER:		++this->typedef_name_specifier; break;
	case CC_TOKEN_TYPE_OF:			++this->typeof_specifier; break;
	case CC_TOKEN_TYPE_OF_UNQUAL:	++this->typeof_unqual_specifier; break;
	case CC_TOKEN_ALIGN_AS:			++this->alignas_specifier; break;
	default:	assert(0); break;
	}
}
/*****************************************************************************/
/*
 * If *out is non-NULL, then it is the parent of the construct being currently
 * parsed. If *out is NULL, then the construct being parsed must create itself
 * and return it.
 */
static
err_t compiler_parse(struct compiler *this,
					 const enum cc_token_type type,
					 struct cc_node *in[],
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
struct cc_node *cc_node_new(const enum cc_token_type type)
{
	struct cc_node *this = calloc(1, sizeof(*this));
	cc_node_init(this);
	this->type = type;
	return this;
}

static
void cc_node_delete(void *p)
{
	/* TODO free based on type */
	struct cc_node *this = p;
	ptrt_empty(&this->tree);
	free(this);
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
	enum cc_name_space_type name_space = CC_NAME_SPACE_ORDINARY;
	/* type-def-names are in ordinary name-space */

	num_entries = 0;
	while (this) {
		PTRQ_FOR_EACH(&this->entries[name_space], i, ste) {
			node = ste->symbol;
			if (node->string == NULL)
				continue;
			if (strcmp(node->string, name))
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
/*****************************************************************************/
err_t compiler_build_types(struct compiler *this)
{
	err_t err;
	int i;
	char *str;
	struct cc_node	*node;
	struct cc_symtab_entry *ste;
	static const enum cc_token_type types[] = {
		CC_TOKEN_BOOL,
		CC_TOKEN_CHAR,
		CC_TOKEN_SHORT,
		CC_TOKEN_INT,
		CC_TOKEN_LONG,
		CC_TOKEN_IDENTIFIER,	/* 'long long' isn't a single token. */
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
		node = malloc(sizeof(*node));
		ste = malloc(sizeof(*ste));
		if (ste == NULL || node == NULL)
			return ENOMEM;
		cc_node_init(node);
		node->type = types[i];
		node->out_type = ste;	/* Points to self */
		if (node->type == CC_TOKEN_IDENTIFIER) {
			node->string = str = malloc(strlen("long long") + 1);
			if (str == NULL)
				return ENOMEM;
			strcpy(str, "long long");
		}
		ste->parent = this->symbols;
		ste->symbol = node;
		ste->type = CC_SYMTAB_ENTRY_INTEGER;
		ste->linkage = CC_LINKAGE_NONE;		/* types have no linkages */
		ste->storage = CC_TOKEN_INVALID;	/* types have no stroage durn */
		ste->name_space = CC_NAME_SPACE_ORDINARY;
		memcpy(&ste->u.integer, &ints[i], sizeof(ints[i]));
		err = cc_symtab_add_entry(this->symbols, ste);
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
	this->root = NULL;
	this->symbols = malloc(sizeof(*this->symbols));
	if (this->symbols == NULL) {
		err = ENOMEM;
		goto err1;
	}
	cc_symtab_init(this->symbols);
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
	enum cc_token_type type;
	struct cc_node *root;

	/* TranslationUnit is the root of ast; array of ExternalDeclaration */
	root = cc_node_new(CC_TOKEN_TRANSLATION_UNIT);
	if (root == NULL)
		return ENOMEM;

	/* Parse array of ExternalDeclaration */
	type = CC_TOKEN_EXTERNAL_DECLARATION;
	while (true) {
		err = compiler_parse(this, type, &root, NULL);
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
		type = CC_TOKEN_STATIC_ASSERT_DECLARATION;
		/*
		 * Since we want to store the StaticAssertDeclarationn within
		 * TranslationUnit, pass TranslationUnit as the parent.
		 */
		return compiler_parse(this, type, in, NULL);
	}

	/* Is it an AttributeDeclaration? */
	attributes = NULL;
	if (cc_token_type(token) == CC_TOKEN_LEFT_BRACKET) {
		type = CC_TOKEN_ATTRIBUTE_SPECIFIER_SEQUENCE;
		err = compiler_parse(this, type, NULL, &attributes);
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

	/* Is it a FunctionDefinition. */
	specifiers = declarator = definition = declaration = NULL;
	type = CC_TOKEN_DECLARATION_SPECIFIERS;
	err = compiler_parse(this, type, NULL, &specifiers);
	if (!err) {
		type = CC_TOKEN_DECLARATOR;
		err = compiler_parse(this, type, NULL, &declarator);
	}
	if (err)
		return err;

	nodes[0] = parent;		/* TranslationUnit */
	nodes[1] = attributes;	/* may be null */
	nodes[2] = specifiers;
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
	return compiler_parse(this, type, nodes, NULL);
}

static
err_t compiler_parse_declaration_specifiers(struct compiler *this,
											struct cc_node **out)
{
	err_t err;
	int num_type_specifiers;
	struct ptr_queue stes;
	enum cc_token_type type;
	const struct cc_symtab_entry *ste;
	struct cc_node *specifiers;
	struct cc_token_stream *stream;
	struct cc_token *token;
	struct cc_storage_class_specifiers scs = {0};
	struct cc_function_specifiers fs = {0};
	struct cc_type_qualifiers tq = {0};
	struct cc_type_specifiers ts = {0};

	assert(*out == NULL);	/* No parent. Create ourselves and return */

	/*
	 * array of DeclarationSpecifier elements, each element optionally followed
	 * by AttributeSpecifierSequence. Check errors too.
	 */
	stream = &this->stream;
	specifiers = cc_node_new(CC_TOKEN_DECLARATION_SPECIFIERS);
	if (specifiers == NULL)
		return ENOMEM;
	num_type_specifiers = 0;
	token = NULL;
	while (true) {
		/* We do not expect an error, not even an EOF */
		err = cc_token_stream_peek_head(stream, &token);
		if (err)
			return err;

		/*
		 * StorageClassSpecifiers, TypeQualifiers, and FunctionSpecifiers are
		 * keywords. Some TypeSpecifiers are compound constructs.
		 * bit-int,atomic,struct,union,enum,typeof need more processing
		 */
		type = cc_token_type(token);

		if (type == CC_TOKEN_IDENTIFIER) {
			/* Should be a TypedefName. If not, break */
			ptrq_init(&stes, NULL);
			err = cc_symtab_find_typedef(this->symbols, cc_token_string(token),
										 &stes);
			if (err == ENOENT)
				break;
			if (err)
				return err;

			/*
			 * Found a type-specifier. If there already is a type-specifier,
			 * raise an error.
			 */
			if (num_type_specifiers)
				return EINVAL;
			++num_type_specifiers;
			cc_type_specifiers_incr(&ts, type);
			err = cc_token_stream_remove_head(stream, &token);
			assert(err == ESUCCESS);
			cc_token_delete(token);

			/* Empty the ste queue */
			PTRQ_FOR_EACH_WITH_REMOVE(&stes, ste);
			goto check_attributes;
		}

		/* TypeSpecifiers */
		if (type == CC_TOKEN_BIT_INT ||
			type == CC_TOKEN_STRUCT ||
			type == CC_TOKEN_UNION ||
			type == CC_TOKEN_ENUM ||
			type == CC_TOKEN_TYPE_OF ||
			type == CC_TOKEN_TYPE_OF_UNQUAL) {
			cc_type_specifiers_incr(&ts, type);
			type = CC_TOKEN_DECLARATION_SPECIFIER;
			err = compiler_parse(this, type, &specifiers, NULL);
			if (err)
				return err;
			goto check_attributes;
		}

		/* Belongs to either TypeQualifier, or TypeSpecifier */
		if (type == CC_TOKEN_ATOMIC) {
			err = cc_token_stream_peek_entry(stream, 1, &token);
			if (!err && cc_token_type(token) == CC_TOKEN_LEFT_PAREN) {
				/* TypeSpecifier, because _Atomic left-paren */
				cc_type_specifiers_incr(&ts, type);
				type = CC_TOKEN_DECLARATION_SPECIFIER;
				err = compiler_parse(this, type, &specifiers, NULL);
				if (err)
					return err;
				goto check_attributes;
			}
			/* Else just a qualifier, and not a full-fledged specifier */
		}

		/* All key-words only */
		if (cc_token_is_storage_class_specifier(token))
			cc_storage_class_specifiers_incr(&scs, type);
		else if (cc_token_is_function_specifier(token))
			cc_function_specifiers_incr(&fs, type);
		else if (cc_token_is_type_qualifier(token))
			cc_type_qualifiers_incr(&tq, type);
		else if (cc_token_is_type_specifier(token))
			cc_type_specifiers_incr(&ts, type);
		else
			break;	/* Something else */
		err = cc_token_stream_remove_head(stream, &token);
		assert(err == ESUCCESS);
		cc_token_delete(token);
check_attributes:
		/* Does an AttributeSpecifierSequence follow? */
		err = cc_token_stream_peek_head(stream, &token);
		if (err || cc_token_type(token) != CC_TOKEN_LEFT_BRACKET)
			continue;	/* no attrs follow */

		type = CC_TOKEN_ATTRIBUTE_SPECIFIER_SEQUENCE;
		err = compiler_parse(this, type, &specifiers, NULL);
		if (err)
			return err;
	}
	assert(err == ESUCCESS);
	/* TODO: Check if there are any errors in the specifiers. */
	*out = specifiers;
	return err;
}

/* Called only to parse compound declaration specifiers like bit-int etc. */
static
err_t compiler_parse_declaration_specifier(struct compiler *this,
										   struct cc_node *in[])
{
	err_t err;
	bool need_parse;
	enum cc_token_type type;
	struct cc_node *specifiers, *specifier, *attributes, *parent;
	struct cc_token_stream *stream;
	struct cc_token *token;

	parent = in[0];
	assert(parent);
	assert(cc_node_type(parent) == CC_TOKEN_DECLARATION_SPECIFIERS);

	need_parse = false;
	err = cc_token_stream_remove_head(stream, &token);
	assert(err == ESUCCESS);
	type = cc_token_type(token);
	/*
	 * StorageClassSpecifiers, TypeQualifiers, and FunctionSpecifiers are
	 * keywords. Some TypeSpecifiers compound constructs.
	 */
	if (cc_token_is_storage_class_specifier(token))
		cc_storage_class_specifiers_incr(&scs, type);
	else if (cc_token_is_type_qualifier(token))
		cc_type_qualifiers_incr(&tq, type);
	else if (cc_token_is_function_specifier(token))
		cc_function_specifiers_incr(&fs, type);
	else if (!cc_token_is_type_specifier(token))
		break;

	/* bit-int,atomic,struct,union,enum,typeof need more processing */
	cc_token_delete(token);
	token = NULL;
	if (type == CC_TOKEN_BIT_INT) {
		/* No change in type. Need parsing */
		need_parse = true;
		cc_type_specifiers_incr(&ts, type);
	} else if (type == CC_TOKEN_ATOMIC) {
		type = CC_TOKEN_ATOMIC_TYPE_SPECIFIER;
		need_parse = true;
		cc_type_specifiers_incr(&ts, type);
	} else if (type == CC_TOKEN_STRUCT ||
			   type == CC_TOKEN_UNION) {
		type = CC_TOKEN_STRUCT_OR_UNION_SPECIFIER;
		need_parse = true;
		cc_type_specifiers_incr(&ts, type);
	} else if (type == CC_TOKEN_ENUM) {
		type = CC_TOKEN_ENUM_SPECIFIER;
		need_parse = true;
		cc_type_specifiers_incr(&ts, type);
	} else if (type == CC_TOKEN_TYPE_OF) {
		type = CC_TOKEN_TYPE_OF_SPECIFIER;
		need_parse = true;
		cc_type_specifiers_incr(&ts, type);
	} else if (type == CC_TOKEN_TYPE_IDENTIFIER) {
		/*
		 * Check if the sym-tabs if this is a type-def-name.
		 * If it is, then continue. If not, then break.
		 */
		cc_type_specifiers_incr(&ts, type);
		/* No need for parsing */
	} else {
		cc_type_specifiers_incr(&ts, type);
		/* No need for parsing */
	}

	if (need_parse)
		err = compiler_parse(this, type, NULL, &specifier);

}

static
err_t compiler_parse(struct compiler *this,
					 const enum cc_token_type type,
					 struct cc_node *in[],
					 struct cc_node **out)
{
	err_t err;

	switch (type) {
	case CC_TOKEN_TRANSLATION_UNIT:
		assert(in == NULL);
		err = compiler_parse_translation_unit(this, out);
		break;
	case CC_TOKEN_EXTERNAL_DECLARATION:
		/* Must be the parent (TranslationUnit) */
		assert(in[0]);

		/* Since the parent is passed in in[], no need to pass out */
		assert(out == NULL);
		err = compiler_parse_external_declaration(this, in);
		break;
	case CC_TOKEN_DECLARATION_SPECIFIERS:
		/* No need for a parent, but we do need out to be non-NULL */
		assert(in == NULL);
		assert(out);
		err = compiler_parse_declaration_specifiers(this, out);
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
	if (cc_node_is_identifier(this) && ! cc_node_is_key_word(this))
		printf("%s", this->u.identifier.string);
	else if (cc_node_is_number(this))
		printf("%s", this->u.number.string);
	else if (cc_node_is_char_const(this))
		printf("%s", this->u.char_const.string);
	else if (cc_node_is_string_literal(this))
		printf("%s", this->u.string_literal.string);
	else
		printf("%s", &g_cc_token_type_str[this->type][strlen("CC_TOKEN_")]);
	PTRT_FOR_EACH_CHILD(&this->tree, i, child)
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
	enum cc_token_type type;

	type = CC_TOKEN_TRANSLATION_UNIT;
	err = compiler_parse(this, type, NULL, &this->root);
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
