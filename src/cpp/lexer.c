/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include "lexer.h"

#include <inc/unicode.h>
#include <inc/types.h>

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>

#include <sys/mman.h>
#include <sys/stat.h>

static
err_t	lexer_peek_code_point(struct lexer *this,
							  struct code_point *out);
static
void lexer_consume_code_point(struct lexer *this,
							  const struct code_point *cp);
static
err_t	lexer_lex_identifier(struct lexer *this,
							 struct lexer_token *out);
/*****************************************************************************/
static const char *g_lexer_token_type_strs[] = {
#define DEF(t)	"LXR_" # t,
#include <inc/cpp/tokens.h>
#undef DEF
};

static const char *g_punctuators[] = {
	/* Single-char tokens */
	"{",
	"[",
	"(",
	"}",
	"]",
	")",
	"\\",
	"@",
	"~",
	"#",
	".",
	":",
	";",
	",",
	"=",
	"/",
	"%",
	"*",
	"+",
	"-",
	"?",
	"!",
	"|",
	"^",
	"&",
	"<",
	">",

	/* Double-char tokens */
	"##",
	"::",
	"++",
	"--",
	"<<",
	">>",
	"||",
	"&&",
	"<=",
	">=",
	"==",
	"!=",
	"*=",
	"/=",
	"%=",
	"+=",
	"-=",
	"&=",
	"|=",
	"^=",

	/* Triple-char tokens */
	"<<=",
	">>=",
	"...",
};

static const char *g_key_words[] = {
	"_Atomic",
	"_BitInt",
	"_Complex",
	"_Decimal128",
	"_Decimal32",
	"_Decimal64",
	"_Generic",
	"_Imaginary",
	"_Noreturn",
	"alignas",
	"alignof",
	"auto",
	"bool",
	"break",
	"case",
	"char",
	"const",
	"constexpr",
	"continue",
	"default",
	"do",
	"double",
	"else",
	"enum",
	"extern",
	"false",
	"float",
	"for",
	"goto",
	"if",
	"inline",
	"int",
	"long",
	"nullptr",
	"register",
	"restrict",
	"return",
	"short",
	"signed",
	"sizeof",
	"static",
	"static_assert",
	"struct",
	"switch",
	"thread_local",
	"true",
	"typedef",
	"typeof",
	"typeof_unqual",
	"union",
	"unsigned",
	"void",
	"volatile",
	"while",

	/* CPP key-words */

	/* Mandatory macros */
	"__DATE__",
	"__FILE__",
	"__LINE__",
	"__STDC__",
	"__STDC_EMBED_NOT_FOUND__",
	"__STDC_EMBED_FOUND__",
	"__STDC_EMBED_EMPTY__",
	"__STDC_HOSTED__",
	"__STDC_NO_ATOMICS__",
	"__STDC_NO_COMPLEX__",
	"__STDC_NO_THREADS__",
	"__STDC_NO_VLA__",
	"__STDC_UTF_16__",
	"__STDC_UTF_32__",
	"__STDC_VERSION__",
	"__TIME__",
	"__VA_ARGS__",
	"__VA_OPT__",
	"__cplusplus",
	"__has_c_attribute",
	"__has_embed",
	"__has_include",
	"define",
	"defined",
	"elif",
	"elifdef",
	"elifndef",
	"embed",
	"endif",
	"error",
	"ifdef",
	"ifndef",
	"include",
	"line",
	"pragma",
	"undef",
	"warning",
};
/*****************************************************************************/
/* replace cr/crlf with lf */
err_t transform_new_lines(const char *src,
						  const off_t src_size,
						  char **out_dst,
						  off_t *out_dst_size)
{
	off_t off, left;
	char *dst;
	const char *ptr;
	char32_t cp;
	char cp_size;
	mbstate_t state;
	bool has_cr;

	if (src_size == 0) {
		*out_dst_size = 0;
		*out_dst = NULL;
		return ESUCCESS;
	}

	/* replace cr-lf or cr with lf */
	dst = malloc(src_size);
	if (dst == NULL)
		return ENOMEM;

	memset(&state, 0, sizeof(state));
	ptr = src;
	has_cr = false;
	off = 0;

	for (left = src_size, ptr = src; left; left -= cp_size, ptr += cp_size) {
		cp_size = mbrtoc32(&cp, ptr, left, &state);
		if (cp_size < 0)
			return EINVAL;

		if (cp_size == 0)	/* nul char */
			cp_size = 1;

		if (cp != '\r' && cp != '\n') {
			/* Keep the dst in utf-8 encoding */
			memcpy(&dst[off], ptr, cp_size);
			off += cp_size;
			continue;
		}

		/* Eagerly replace cr with lf */
		if (cp == '\r') {
			has_cr = true;
			dst[off++] = '\n';
			continue;
		}

		assert(cp == '\n');
		if (has_cr) {
			has_cr = false;
			continue;
		}
		dst[off++] = '\n';
	}
	*out_dst_size = off;
	*out_dst = dst;
	return ESUCCESS;
}

static
err_t lexer_read_file(struct lexer *this,
					  const char *path)
{
	err_t err;
	int ret, fd;
	char *str, *dst;
	const char *src, *dir_path;
	off_t src_size, dst_size;
	struct stat stat;

	err = ESUCCESS;

	str = strdup(path);
	if (str == NULL) {
		err = ENOMEM;
		goto err0;
	}

	dir_path = dirname(str);
	if (dir_path == NULL) {
		err = ENOMEM;
		goto err1;
	}

	dir_path = strdup(dir_path);
	if (dir_path == NULL) {
		err = ENOMEM;
		goto err1;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		err = errno;
		goto err2;
	}

	ret = fstat(fd, &stat);
	if (ret < 0) {
		err = errno;
		goto err3;
	}

	src_size = stat.st_size;
	/* Empty files are no problem */
	if (src_size) {
		src = mmap(NULL, src_size, PROT_READ, MAP_SHARED, fd, 0);
		if (src == MAP_FAILED) {
			err = errno;
			goto err3;
		}
		err = transform_new_lines(src, src_size, &dst, &dst_size);
		munmap((void *)src, src_size);
	} else {
		dst = NULL;
		dst_size = 0;
	}
	close(fd);
	if (err)
		goto err2;

	this->file_path = str;
	this->dir_path = dir_path;
	this->buffer = dst;
	this->buffer_size = dst_size;
	return err;
err3:
	close(fd);
err2:
	free((void *)dir_path);
err1:
	free(str);
err0:
	return err;
}

err_t lexer_new(const char *path,
				const char *buffer,
				const off_t buffer_size,
				struct lexer **out)
{
	err_t err;
	struct lexer *this;

	this = malloc(sizeof(*this));
	if (this == NULL) {
		err = ENOMEM;
		goto err0;
	}

	this->file_path = NULL;
	this->dir_path = NULL;
	this->buffer = buffer;
	this->buffer_size = buffer_size;
	this->position.lex_pos = 0;
	this->position.file_row = 0;
	this->position.file_col = 0;

	err = ESUCCESS;
	if (path)
		err = lexer_read_file(this, path);
	if (err)
		goto err1;
	*out = this;
	return err;
err1:
	free(this);
err0:
	return err;
}

err_t lexer_delete(struct lexer *this)
{
	free((void *)this->buffer);
	free((void *)this->dir_path);
	free((void *)this->file_path);
	free(this);
	return ESUCCESS;
}
/*****************************************************************************/
static
void lexer_token_delete(struct lexer_token *this)
{
	enum lexer_token_type type;

	assert(this->ref_count == 0);
	type = lexer_token_type(this);
	if (lexer_token_is_char_const(this) ||
		lexer_token_is_string_literal(this) ||
		type == LXR_TOKEN_NUMBER ||
		type == LXR_TOKEN_IDENTIFIER) {
		free((void *)this->source);
		if (this->source != this->resolved)
			free((void *)this->resolved);
	}
}

void lexer_token_deref(struct lexer_token *this)
{
	assert(this);
	if (--this->ref_count == 0) {
		lexer_token_delete(this);
		free(this);
	}
}

static
err_t lexer_build_source(struct lexer *this,
						 struct lexer_token *token)
{
	err_t err;
	bool has_esc_seq, was_prev_back_slash;
	size_t size, i;
	enum lexer_token_type type;
	struct lexer_position save;
	struct code_point cp;
	char *string;

	assert(token->source == NULL);
	assert(token->resolved == NULL);
	assert(token->source_len == 0);
	assert(token->resolved_len == 0);

	type = lexer_token_type(token);

	/* punctuators do not need to alloc memory for storing src/reslvd */
	if (type >= LXR_TOKEN_LEFT_BRACE && type <= LXR_TOKEN_ELLIPSIS) {
		token->source = g_punctuators[type - LXR_TOKEN_LEFT_BRACE];
		goto same_resolved;
	}

	/* key-words do not need to alloc memory for storing src/reslvd */
	if (type >= LXR_TOKEN_ATOMIC && type <= LXR_TOKEN_DIRECTIVE_WARNING) {
		token->source = g_key_words[type - LXR_TOKEN_ATOMIC];
		goto same_resolved;
	}

	switch (type) {
	case LXR_TOKEN_NUMBER:break;	/* fall-thru */
	case LXR_TOKEN_IDENTIFIER:break;	/* fall-thru */
	case LXR_TOKEN_HASH:token->source = "#";break;
	default:assert(0);
	}
same_resolved:
	if (token->source) {
		token->resolved = token->source;
		token->resolved_len = token->source_len = strlen(token->source);
		assert(token->source_len == token->lex_size);
		return ESUCCESS;
	}

	assert(type == LXR_TOKEN_NUMBER ||
		   type == LXR_TOKEN_IDENTIFIER);

	/* These must be rescanned */
	/* Rewind back to the start and rescan the cps */
	size = token->lex_size;
	string = malloc(size + 1);
	if (string == NULL)
		return ENOMEM;

	save = this->position;	/* Save the current position */
	this->position = this->begin;	/* Rewind */
	string[size] = NULL_CHAR;
	has_esc_seq = was_prev_back_slash = false;
	for (i = 0; i < size;) {
		/* only attempt to read more cps if size is non-zero. */
		err = lexer_peek_code_point(this, &cp);
		if (err)
			goto err0;

		if (!has_esc_seq &&
			was_prev_back_slash &&
			(cp.cp == 'u' ||
			 cp.cp == 'U' ||
			 cp.cp == 'x' ||
			 is_oct_digit(cp.cp)))
			has_esc_seq = true;
		was_prev_back_slash = cp.cp == '\\';
		memcpy(&string[i], &cp.cp, cp.cp_size);
		i += cp.cp_size;
		lexer_consume_code_point(this, &cp);
	}
	token->source = string;
	assert(has_esc_seq == false);	/* TODO */
	if (!has_esc_seq) {
		token->resolved = token->source;
		token->resolved_len = token->source_len = token->lex_size;
	}
	/* fall-thru */
err0:
	this->position = save;
	return err;
}

#if 0
static
err_t code_point_to_utf8(const char32_t this,
						 char *out_num_code_units,
						 char *out_code_units)	/* at least 4 chars */
{
	int ret;
	char num_code_units;
	mbstate_t state;
	char cu[4];

	if (out_code_units == NULL)
		out_code_units = cu;
	memset(&state, 0, sizeof(state));
	num_code_units = ret = c32rtomb(out_code_units, this, &state);
	if (ret <= 0)
		return EINVAL;
	if (out_num_code_units)
		*out_num_code_units = num_code_units;
	return ESUCCESS;
}

static
err_t code_point_to_utf16(const char32_t this,
						  char *out_num_code_units,
						  char16_t *out_code_units)	/* at least 2 char16s */
{
	int ret;
	err_t err;
	char utf8_units[4];
	mbstate_t state;

	err = code_point_to_utf8(this, out_num_code_units, utf8_units);
	if (err)
		return err;

	/*
	 * For a code-point that needs surrogate pairs for encoding with
	 * utf-16, mbrtoc16 first consumes the entire code-point (represented
	 * in cp_bytes as utf-8 multibyte, and evident from its return value),
	 * and stores the high surrogate in c16.
	 */

	memset(&state, 0, sizeof(state));
	ret = mbrtoc16(&out_code_units[0], utf8_units, 4, &state);
	if (ret <= 0)
		return EINVAL;
	*out_num_code_units = 1;
	if (is_high_surrogate(out_code_units[0])) {
		ret = mbrtoc16(&out_code_units[1], utf8_units, 4, &state);
		assert(ret == -3);
		assert(is_low_surrogate(out_code_units[1]));
		*out_num_code_units = 2;
	}
	return ESUCCESS;
}
#endif
/*****************************************************************************/

void lexer_token_init(struct lexer_token *this)
{
	memset(this, 0, sizeof(*this));
	this->value = -1;
}
#if 0
void lexer_token_print_string(const struct lexer_token *this)
{
	int i;
	enum lexer_token_type type;

	/* TODO print utf16/32. */
	/* Note that strings are not delimited by nul */
	type = lexer_token_type(this);
	if (type == LXR_TOKEN_UTF_16_STRING_LITERAL ||
		type == LXR_TOKEN_UTF_32_STRING_LITERAL ||
		type == LXR_TOKEN_WCHAR_T_STRING_LITERAL ||
		type == LXR_TOKEN_UTF_16_CHAR_CONST ||
		type == LXR_TOKEN_UTF_32_CHAR_CONST ||
		type == LXR_TOKEN_WCHAR_T_CHAR_CONST)
		return;
	for (i = 0; i < this->string_len; ++i)
		printf("%c", this->utf8_string[i]);
}
#endif
void lexer_token_print(const struct lexer_token *this,
					   const struct lexer_position *begin,
					   const char *msg)
{
	const char *type = g_lexer_token_type_strs[this->type];
	printf("%s: pos %ld, file (%ld,%ld), ws? %d, 1st? %d, %s: '%s'",
		   msg,
		   begin->lex_pos,
		   begin->file_row + 1,
		   begin->file_col + 1,
		   this->has_white_space,
		   this->is_first,
		   type,
		   this->source);
	printf("\n");
}

/*
 * Conversion of a string will always be from char-string/utf8-string to
 * either char16-str or char32-str
 */
#if 0
static
err_t lexer_token_convert_string_to_utf32(struct lexer_token *this)
{
	int ret;
	off_t string_len, i, out_string_len;
	char32_t code_point;
	char32_t *utf32_string;
	const char *string;
	mbstate_t state;

	assert(this->utf16_string == NULL);
	assert(this->utf32_string == NULL);

	utf32_string = NULL;
	string = lexer_token_string(this);
	string_len = lexer_token_string_length(this);
	assert(string);
	assert(string_len);

	out_string_len = 0;
	memset(&state, 0, sizeof(state));
	for (i = 0; i < string_len;) {
		ret = mbrtoc32(&code_point, string + i, string_len - i, &state);
		if (ret < 0)
			return EINVAL;
		if (ret == 0)
			ret = 1;	/* nul char */
		i += ret;
		utf32_string = realloc(utf32_string, (out_string_len + 1) * 2);
		utf32_string[out_string_len++] = code_point;
	}

	free((void *)this->utf8_string);
	this->utf8_string = NULL;
	this->utf32_string = utf32_string;
	this->string_len = out_string_len;
	return ESUCCESS;
}

/* TODO name the array in singular, count in plural */
static
err_t lexer_token_convert_string_to_utf16(struct lexer_token *this)
{
	int ret;
	off_t string_len, i, out_string_len;
	char16_t code_unit[2];
	char num_code_units;
	char16_t *utf16_string;
	const char *string;
	mbstate_t state;

	assert(this->utf16_string == NULL);
	assert(this->utf32_string == NULL);

	utf16_string = NULL;
	string = lexer_token_string(this);
	string_len = lexer_token_string_length(this);
	assert(string);
	assert(string_len);

	out_string_len = 0;
	memset(&state, 0, sizeof(state));
	for (i = 0; i < string_len;) {
		ret = mbrtoc16(&code_unit[0], string + i, string_len - i, &state);
		if (ret < 0)
			return EINVAL;
		if (ret == 0)
			ret = 1;	/* nul char */
		i += ret;
		num_code_units = 1;
		if (is_high_surrogate(code_unit[0])) {
			ret = mbrtoc16(&code_unit[1], string + i, string_len - i, &state);
			assert(ret == -3);
			++num_code_units;
			assert(is_low_surrogate(code_unit[1]));
		}
		out_string_len += num_code_units;
		utf16_string = realloc(utf16_string, out_string_len * 2);
		out_string_len -= num_code_units;
		utf16_string[out_string_len++] = code_unit[0];
		if (num_code_units == 2)
			utf16_string[out_string_len++] = code_unit[1];
	}

	free((void *)this->utf8_string);
	this->utf8_string = NULL;
	this->utf16_string = utf16_string;
	this->string_len = out_string_len;
	return ESUCCESS;
}

/* TODO name the array in singular, count in plural */
err_t lexer_token_concat_strings(struct lexer_token *this,
								 struct lexer_token *next)
{
	err_t err;
	off_t len[2], string_len;
	enum lexer_token_type type[2];
	char *utf8_string;
	char16_t *utf16_string;
	char32_t *utf32_string;

	utf8_string = (char *)this->utf8_string;
	utf16_string = (char16_t *)this->utf16_string;
	utf32_string = (char32_t *)this->utf32_string;

	len[0] = lexer_token_string_length(this);
	len[1] = lexer_token_string_length(next);
	assert(len[0] >= 2);
	assert(len[1] >= 2);
	if (len[1] == 2)
		return ESUCCESS;

	type[0] = lexer_token_type(this);
	type[1] = lexer_token_type(next);
	if (type[0] != type[1])
		goto disjoint_types;	/* goto helps reduce the indentation level */

	assert(type[0] == type[1]);
	this->string_len = string_len = len[0] + len[1] - 2;
	if (type[0] == LXR_TOKEN_CHAR_STRING_LITERAL ||
		type[0] == LXR_TOKEN_UTF_8_STRING_LITERAL) {
		assert(utf8_string != NULL);
		assert(utf16_string == NULL);
		assert(utf32_string == NULL);
		assert(next->utf8_string != NULL);
		assert(next->utf16_string == NULL);
		assert(next->utf32_string == NULL);
		utf8_string = realloc(utf8_string, string_len);
		if (utf8_string == NULL)
			return ENOMEM;
		memcpy(&utf8_string[len[0] - 1], &next->utf8_string[1],
			   len[1] - 1);
		this->utf8_string = utf8_string;
		return ESUCCESS;
	}

	if (type[0] == LXR_TOKEN_UTF_16_STRING_LITERAL) {
		assert(utf8_string == NULL);
		assert(utf16_string != NULL);
		assert(utf32_string == NULL);
		assert(next->utf8_string == NULL);
		assert(next->utf16_string != NULL);
		assert(next->utf32_string == NULL);
		utf16_string = realloc(utf16_string, string_len * 2);
		if (utf16_string == NULL)
			return ENOMEM;
		memcpy(&utf16_string[len[0] - 1], &next->utf16_string[1],
			   (len[1] - 1) * 2);
		this->utf16_string = utf16_string;
		return ESUCCESS;
	}
	assert(type[0] == LXR_TOKEN_UTF_32_STRING_LITERAL ||
		   type[0] == LXR_TOKEN_WCHAR_T_STRING_LITERAL);
	assert(utf8_string == NULL);
	assert(utf16_string == NULL);
	assert(utf32_string != NULL);
	assert(next->utf8_string == NULL);
	assert(next->utf16_string == NULL);
	assert(next->utf32_string != NULL);
	this->utf32_string = utf32_string = realloc(utf32_string, string_len * 4);
	if (utf32_string == NULL)
		return ENOMEM;
	memcpy(&utf32_string[len[0] - 1], &next->utf32_string[1],
		   (len[1] - 1) * 4);
	this->utf32_string = utf32_string;
	return ESUCCESS;

disjoint_types:
	/* one must be char-str/utf 8 */
	assert(type[0] == LXR_TOKEN_CHAR_STRING_LITERAL ||
		   type[0] == LXR_TOKEN_UTF_8_STRING_LITERAL ||
		   type[1] == LXR_TOKEN_CHAR_STRING_LITERAL ||
		   type[1] == LXR_TOKEN_UTF_8_STRING_LITERAL);

	/* disjoint types. pick the one that is not utf8/char-string */
	err = ESUCCESS;
	if (type[0] == LXR_TOKEN_UTF_16_STRING_LITERAL)
		err = lexer_token_convert_string_to_utf16(next);
	if (type[0] == LXR_TOKEN_UTF_32_STRING_LITERAL)
		err = lexer_token_convert_string_to_utf32(next);
	if (type[1] == LXR_TOKEN_UTF_16_STRING_LITERAL)
		err = lexer_token_convert_string_to_utf16(this);
	if (type[1] == LXR_TOKEN_UTF_32_STRING_LITERAL)
		err = lexer_token_convert_string_to_utf32(this);
	if (err)
		return err;
	type[0] = lexer_token_type(this);
	type[1] = lexer_token_type(next);
	assert(type[0] == type[1]);
	return lexer_token_concat_strings(this, next);
}
#endif
/*****************************************************************************/
static
err_t lexer_decode_code_point(const struct lexer *this,
							  const off_t lex_pos,
							  struct code_point *out)
{
	off_t left;
	const char *ptr;
	mbstate_t state;
	char32_t cp;
	int cp_size;

	left = this->buffer_size - lex_pos;
	if (left <= 0)
		return EOF;

	ptr = this->buffer + lex_pos;
	memset(&state, 0, sizeof(state));

	cp_size = mbrtoc32(&cp, ptr, left, &state);
	if (cp_size < 0)
		return EINVAL;
	if (cp_size == 0)	/* nul char */
		cp_size = 1;
	out->cp_size = cp_size;
	out->cp = cp;
	return ESUCCESS;
}

static
void lexer_consume_code_point(struct lexer *this,
							  const struct code_point *cp)
{
	/* cp must be the code-point scanned at this->position */
	assert(this->position.lex_pos == cp->begin.lex_pos);
	this->position.lex_pos += cp->cp_size;
	++this->position.file_col;
	if (cp->cp == '\n') {
		++this->position.file_row;
		this->position.file_col = 0;
	}
}

static
err_t lexer_peek_code_point(struct lexer *this,
							struct code_point *out)
{
	err_t err;
	off_t lex_pos;
	struct code_point cp = {0};	/* compiler warning */

	out->begin = this->position;
	lex_pos = out->begin.lex_pos;
	err = lexer_decode_code_point(this, lex_pos, out);
	if (err)
		return err;
	if (out->cp != '\\')
		return ESUCCESS;

	assert(out->cp_size == 1);
	++lex_pos;	/* consume back-slash */

	/* if bs is not followed by a nl, emit only bs. */
	err = lexer_decode_code_point(this, lex_pos, &cp);
	if (err || cp.cp != '\n')
		return ESUCCESS;
	assert(cp.cp_size == 1);

	/* bs followed by nl. consume both and continue to peek. */
	lexer_consume_code_point(this, out);	/* bs */
	cp.begin = this->position;
	lexer_consume_code_point(this, &cp);	/* nl */
	return lexer_peek_code_point(this, out);
}


/*
 * TODO:
 *	Convert \U and \u universal-char-names into utf-8.
 *	Conver \char to char
 */
#if 0
err_t lexer_build_token_string(struct lexer *this,
							   struct lexer_token *token)
{
	err_t err;
	off_t i;
	size_t size;
	struct lexer_position save;
	struct code_point cp;
	char *string;

	err = ESUCCESS;

	/* Don't build for strings and char-const */
	if (lexer_token_is_char_const(token) ||
		lexer_token_is_string_literal(token))
		return err;

	/* Rewind back to the start and rescan the cps */
	string = malloc(token->lex_size + 1);
	if (string == NULL) {
		err = ENOMEM;
		goto err0;
	}

	save = this->position;	/* Save the current position */

	/* Rewind the scanner position */
	this->position = token->begin;
	size = token->lex_size;
	i = 0;
	while (size) {
		/* only attempt to read more cps if size is non-zero. */
		err = lexer_peek_code_point(this, &cp);
		if (err)
			goto err1;
		memcpy(&string[i], &cp.cp, cp.cp_size);
		i += cp.cp_size;
		size -= cp.cp_size;
		lexer_consume_code_point(this, &cp);
	}
	string[i] = 0;
	token->utf8_string = string;
	token->string_len = i;
	/* fall-thru */
err1:
	this->position = save;
err0:
	return err;
}
#endif
/*****************************************************************************/
static
void lexer_skip_single_line_comment(struct lexer *this)
{
	err_t err;
	struct code_point cp;

	while (true) {
		err = lexer_peek_code_point(this, &cp);
		if (err || cp.cp == '\n')	/* new-line isn't a part of the comment */
			break;
		lexer_consume_code_point(this, &cp);
	}
}

static
void lexer_skip_multi_line_comment(struct lexer *this)
{
	err_t err;
	struct code_point cp;
	bool half_close_seen;

	half_close_seen = false;
	while (true) {
		err = lexer_peek_code_point(this, &cp);
		if (err)
			break;
		lexer_consume_code_point(this, &cp);
		if (cp.cp == '*')
			half_close_seen = true;
		else if (cp.cp == '/' && half_close_seen)
			break;
		else
			half_close_seen = false;
	}
}
/*****************************************************************************/
static
err_t lexer_lex_ucn_escape_char(struct lexer *this,
								const int num_digits,
								struct lexer_token *out)
{
	err_t err;
	int i;
	char32_t t[2];
	struct code_point cp;

	t[0] = t[1] = 0;
	for (i = 0; i < num_digits; ++i) {
		err = lexer_peek_code_point(this, &cp);
		if (err)
			return err;
		if (!is_hex_digit(cp.cp))
			return EINVAL;

		t[0] = t[1] << 4;
		if (t[0] < t[1])
			return EINVAL;	/* overflow */
		t[0] += hex_digit_value(cp.cp);
		if (t[0] < t[1])
			return EINVAL;	/* overflow */
		t[1] = t[0];
		lexer_consume_code_point(this, &cp);
		out->lex_size += cp.cp_size;
	}

	/*
	 * The ucn cp must not be less than 0xa0; exceptions: 0x24, 0x40, 0x60.
	 * It must not be in the high/low surrogate range.
	 * It must not be > 0x10ffff.
	 */
	if (t[1] < 0xa0 && t[1] != '$' && t[1] != '@' && t[1] != '`')
		return EINVAL;
	if (t[1] >= 0xd800 && t[1] < 0xe000)
		return EINVAL;
	if (t[1] > 0x10ffff)
		return EINVAL;
	out->value = t[1];
	return err;
}

/* One escape char */
/* max munch until we reach a non-hex char. */
#if 0
static
err_t lexer_lex_hex_escape_char(struct lexer *this,
								struct lexer_token *out)
{
	err_t err;
	char32_t t[2];
	struct code_point cp;

	t[0] = t[1] = 0;
	while (true) {
		err = lexer_peek_code_point(this, &cp);
		if (err)
			return err;
		if (!is_hex_digit(cp.cp))
			break;

		t[0] = t[1] << 4;
		if (t[0] < t[1])
			return EINVAL;	/* overflow */
		t[0] += hex_digit_value(cp.cp);
		if (t[0] < t[1])
			return EINVAL;	/* overflow */
		t[1] = t[0];
		lexer_consume_code_point(this, &cp);
		out->lex_size += cp.cp_size;
	}
	out->code_point = t[1];
	return err;
}

/* One escape char */
/*
 * max munch until we reach a non-oct char or 3 oct-digits,
 * whichever is first.
 */
static
err_t lexer_lex_oct_escape_char(struct lexer *this,
								struct lexer_token *out)
{
	err_t err;
	int i;
	char32_t t[2];
	struct code_point cp;

	/* for non-prefix and utf-8 case, the char must be single byte */
	t[0] = t[1] = 0;
	for (i = 0; i < 3; ++i) {
		err = lexer_peek_code_point(this, &cp);
		if (err)
			return err;
		if (!is_oct_digit(cp.cp))
			break;

		t[0] = t[1] << 3;
		if (t[0] < t[1])
			return EINVAL;	/* overflow */
		t[0] += oct_digit_value(cp.cp);
		if (t[0] < t[1])
			return EINVAL;	/* overflow */
		t[1] = t[0];
		lexer_consume_code_point(this, &cp);
		out->lex_size += cp.cp_size;
	}
	out->code_point = t[1];
	return err;
}

/* One escape char */
static
err_t lexer_lex_escape_char(struct lexer *this,
							enum char_const_escape_type *out_esc_type,
							struct lexer_token *out)
{
	err_t err;
	char32_t c32;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;

	/* Is it a simple esc-seq? */
	*out_esc_type = CHAR_CONST_ESCAPE_SIMPLE;
	if (cp.cp == '\'' || cp.cp == '\"' || cp.cp == '?' || cp.cp == '\\' ||
		cp.cp == 'a' || cp.cp == 'b' || cp.cp == 'f' || cp.cp == 'n' ||
		cp.cp == 'r' || cp.cp == 't' || cp.cp == 'v') {
		lexer_consume_code_point(this, &cp);
		out->lex_size += cp.cp_size;
		/*
		 * Although the lex_size of simple escs is 2, the value is a single
		 * code-unit.
		 */
		if (cp.cp == '\'' || cp.cp == '\"' || cp.cp == '?' || cp.cp == '\\')
			out->code_point = cp.cp;
		if (cp.cp == 'a')
			out->code_point = '\a';
		if (cp.cp == 'b')
			out->code_point = '\b';
		if (cp.cp == 'f')
			out->code_point = '\f';
		if (cp.cp == 'n')
			out->code_point = '\n';
		if (cp.cp == 'r')
			out->code_point = '\r';
		if (cp.cp == 't')
			out->code_point = '\t';
		if (cp.cp == 'v')
			out->code_point = '\v';
		return ESUCCESS;
	}

	*out_esc_type = CHAR_CONST_ESCAPE_OCT;
	if (is_oct_digit(cp.cp))
		return lexer_lex_oct_escape_char(this, out);

	*out_esc_type = CHAR_CONST_ESCAPE_HEX;
	if (cp.cp == 'x' || cp.cp == 'u' || cp.cp == 'U') {
		lexer_consume_code_point(this, &cp);	/* consume x/u/U */
		out->lex_size += cp.cp_size;
		c32 = cp.cp;
		err = lexer_peek_code_point(this, &cp);
		if (err)
			return err;
		if (!is_hex_digit(cp.cp))
			return EINVAL;
		if (c32 == 'x')
			return lexer_lex_hex_escape_char(this, out);
		*out_esc_type = CHAR_CONST_ESCAPE_UCN_4;
		if (c32 == 'u')
			return lexer_lex_ucn_escape_char(this, 4, out);
		assert(c32 == 'U');
		*out_esc_type = CHAR_CONST_ESCAPE_UCN_8;
		return lexer_lex_ucn_escape_char(this, 8, out);
	}
	return EINVAL;	/* Invalid escape */
}
/*****************************************************************************/
/* For int-char-const:
 * If the char-const contains >1 chars, or if it contains a char,
 * either directly embedded or thru hex/oct/ucn esc-seq, that
 * maps to multiple values in the literal-encoding, then the value of
 * this char-const is implementation defined.
 *
 * Thus,
 * char c = '<copy-right-symbol>'; is a directly embedded (i.e. not
 * using an esc-seq) char that needs multiple values in the
 * literal-encoding (utf-8 here). But instead of raising an error, like
 * it is done in the case of utf-8/-16/-32, a warning suffices with the
 * value of the char-const as some implementation-defined value.
 *
 * char c = \'u00a9'; raises a warning. 2 chars instead of one.
 *
 * char c = \'U00000000a9'; raises a warning in gcc, an error in clang.
 * 4 chars instead of one.
 *
 * But,
 * char c = '\xa9'; is valid.
 *
 * Similar reasoning for wchar-t.
 */

/*
 * For utf-8:
 * If the char-const is produced by hex/oct esq-seq, the value of the
 * char-const is equal to the numeric value specified in the hex/oct
 * esc-seq. (provided it isn't larger than 0xff. gcc returns a warning
 * in that case, clang raises an error.)
 *
 * If the char-const is produced by ucn or direct embedding of the char
 * in the src, the value of the char-const is equal to the iso/iec
 * 10646 code-point value, provided that the cp-value can be encoded as
 * a single code-unit of the corresponding encoding.
 *
 * a single code-unit in utf-8 is of size 1 byte.
 * a single code-unit in utf-16 is of size 2 bytes.
 * a single code-unit in utf-32 is of size 4 bytes.
 *
 * For e.g., in utf-8 there's no char with encoding 0xa9. The 8-bit
 * ascii 0xa9 (copyright symbol) is encoded in utf-8 as 0xc2 0xa9.
 *
 * Thus,
 * char8_t c = u8'<copy-right-symbol>'; raises error, because the
 * code-point value 0xa9 of the directly embedded char needs two
 * code-units, 0xc2 0xa9, in utf-8.
 *
 * char8_t c = u8'\u00a9'; raises error because of the same reason as
 * above.
 *
 * char8_t c = u8'\U000000a9'; raises error because of the same reason as
 * above.
 *
 * But, char8_t c = u8'\xa9'; works okay without any warnings or
 * errors, even though there's really no char in utf-8 with that
 * encoding.
 *
 * Similar reasoning for utf-16/-32.
 */

static
err_t lexer_lex_char_const_one(struct lexer *this,
							   enum char_const_escape_type *out_esc_type,
							   struct lexer_token *out)
{
	err_t err;
	struct code_point cp;
	enum char_const_escape_type esc_type;
	struct lexer_token token;

	*out_esc_type = esc_type = CHAR_CONST_ESCAPE_NONE;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;

	/* Possible esc-seq */
	if (cp.cp == '\\') {
		lexer_consume_code_point(this, &cp);	/* consume back-slash */
		out->lex_size += cp.cp_size;
		token.lex_size = 0;
		err = lexer_lex_escape_char(this, out_esc_type, &token);
		if (err)
			return err;
		esc_type = *out_esc_type;
		out->lex_size += token.lex_size;
		out->code_point = cp.cp = token.code_point;
		/* cp.cp_size is undefined here when producing cp thru esc-seqs. */

		/*
		 * For simple esc, the code-point size is the same as token's lex-size.
		 * for other escapes, the code-point size must be calculated.
		 */
		if (esc_type == CHAR_CONST_ESCAPE_SIMPLE) {
			cp.cp_size = token.lex_size;
			assert(cp.cp_size == 1);
		} else {
			err = code_point_to_utf8(out->code_point, &cp.cp_size, NULL);
			if (err)
				return err;
		}
	} else {
		/* cp.cp_size known here */
		lexer_consume_code_point(this, &cp);
		out->lex_size += cp.cp_size;
		out->code_point = cp.cp;
	}

	/*
	 * This is a single char situation.
	 *
	 * For int-char-const:
	 * If the char (embedded directly, or thru hex/oct/ucn esc-seq) in this
	 * char-const maps to multiple values in the literal-encoding, the value of
	 * the char-const is impl. defined. Since literal-encoding is utf-8 here,
	 * we raise an error (for now) if the cp_size is > 1.
	 */
	if (out->type == LXR_TOKEN_INTEGER_CHAR_CONST && cp.cp_size > 1)
		return EINVAL;

	/*
	 * For utf-8 char-const:
	 *
	 * If the char-const is produced through hex/oct esc-seq, its value is as
	 * specified by the hex/oct number (provided it is not larger than 0xff.)
	 *
	 * If the char-const is produced directly or through ucn esc-seq, its value
	 * is equal to its iso/iec 10646 code-point value, provided the code-point
	 * value can be encoded as a single code-unit. Note that utf-16 code-unit
	 * is of 16-bits size. This limitation doesn't seem to apply when scanning
	 * a string, as both gcc and clang allow surrogate pairs.
	 */

	if (out->type == LXR_TOKEN_UTF_8_CHAR_CONST) {
		if ((esc_type == CHAR_CONST_ESCAPE_OCT ||
			 esc_type == CHAR_CONST_ESCAPE_HEX) && cp.cp > 0xff)
			return EINVAL;
		/* Produced directly or thru ucn */
		if (cp.cp_size > 1)
			return EINVAL;
	}

	/*
	 * For utf-16 char-const:
	 * If the char-const is produced through hex/oct esc-seq, its value is as
	 * specified by the hex/oct number (provided it is not larger than 0xffff.)
	 *
	 * If the char-const is produced directly or through ucn esc-seq, its value
	 * is equal to its iso/iec 10646 code-point value, provided the code-point
	 * value can be encoded as a single code-unit. Note that utf-16 code-unit
	 * is of 16-bits size. This limitation doesn't seem to apply when scanning
	 * a string, as both gcc and clang allow surrogate pairs.
	 *
	 * This second check for char16_t are provided separately in
	 * lexer_lex_string_literal and lexer_lex_char_const to account for this
	 * difference.
	 */
	if (out->type == LXR_TOKEN_UTF_16_CHAR_CONST) {
		if ((esc_type == CHAR_CONST_ESCAPE_OCT ||
			 esc_type == CHAR_CONST_ESCAPE_HEX) && cp.cp > 0xffff)
			return EINVAL;
	}

	/*
	 * For utf-32 char-const:
	 *
	 * If the char-const is produced through hex/oct esc-seq, its value is as
	 * specified by the hex/oct number. Any possible value from the set of all
	 * 32-bit unsigned ints is allowed.
	 *
	 * If the char-const is produced directly or through ucn esc-seq, its value
	 * is equal to its iso/iec 10646 code-point value, provided the code-point
	 * value can be encoded as a single code-unit. Note that utf-32 code-unit
	 * is of 32-bits size.
	 *
	 * wchar_t is the same as utf-32 in this impl.
	 */

	if ((out->type == LXR_TOKEN_UTF_32_CHAR_CONST ||
		 out->type == LXR_TOKEN_WCHAR_T_CHAR_CONST)) {
		if ((esc_type == CHAR_CONST_ESCAPE_OCT ||
			 esc_type == CHAR_CONST_ESCAPE_HEX)) {
			/* Any value is a valid value. */
		}
		/*
		 * cp.cp_size is assumed to be 4, since there aren't any utf-32 code
		 * points that need two utf-32 code-units (i.e. 8 bytes).
		 */
	}
	return err;
}

static
err_t lexer_token_store_code_point(struct lexer_token *this,
								   const char32_t cp,
								   char *out_num_code_units)
{
	err_t err;
	char *utf8_string;
	char16_t *utf16_string;
	char32_t *utf32_string;
	off_t string_len;
	char utf8_code_units[4];
	char16_t utf16_code_units[2];
	char num_code_units;
	enum lexer_token_type type;

	/*
	 * convert string type to char-const, since both string and char-const
	 * scanners use this func.
	 */
	type = this->type;
	if (type == LXR_TOKEN_CHAR_STRING_LITERAL)
		type = LXR_TOKEN_INTEGER_CHAR_CONST;
	if (type == LXR_TOKEN_UTF_8_STRING_LITERAL)
		type = LXR_TOKEN_UTF_8_CHAR_CONST;
	if (type == LXR_TOKEN_UTF_16_STRING_LITERAL)
		type = LXR_TOKEN_UTF_16_CHAR_CONST;
	if (type == LXR_TOKEN_UTF_32_STRING_LITERAL)
		type = LXR_TOKEN_UTF_32_CHAR_CONST;
	if (type == LXR_TOKEN_WCHAR_T_STRING_LITERAL)
		type = LXR_TOKEN_WCHAR_T_CHAR_CONST;

	utf8_string = (char *)this->utf8_string;
	utf16_string = (char16_t *)this->utf16_string;
	utf32_string = (char32_t *)this->utf32_string;
	/*
	 * Convert the code-point into appropriate code-unit and store into the
	 * correct string field.
	 */
	if (type == LXR_TOKEN_INTEGER_CHAR_CONST ||
		type == LXR_TOKEN_UTF_8_CHAR_CONST) {
		err = code_point_to_utf8(cp, &num_code_units, utf8_code_units);
		if (err)
			return err;
		string_len = this->string_len + num_code_units;
		/* assuming sizeof(char8_t) == 1 */
		assert(sizeof(char) == 1);
		utf8_string = realloc(utf8_string, string_len);
		if (utf8_string == NULL)
			return ENOMEM;
		memcpy(&utf8_string[this->string_len], utf8_code_units,
			   num_code_units);
		this->utf8_string = utf8_string;
		this->string_len = string_len;
		if (out_num_code_units)
			*out_num_code_units = num_code_units;
		return ESUCCESS;
	}

	if (type == LXR_TOKEN_UTF_16_CHAR_CONST) {
		err = code_point_to_utf16(cp, &num_code_units, utf16_code_units);
		if (err)
			return err;
		string_len = this->string_len + num_code_units;
		assert(sizeof(char16_t) == 2);
		utf16_string = realloc(utf16_string, string_len * 2);
		if (utf16_string == NULL)
			return ENOMEM;
		memcpy(&utf16_string[this->string_len], utf16_code_units,
			   num_code_units * 2);
		this->utf16_string = utf16_string;
		this->string_len = string_len;
		if (out_num_code_units)
			*out_num_code_units = num_code_units;
		return ESUCCESS;
	}

	assert(type == LXR_TOKEN_UTF_32_CHAR_CONST ||
		   type == LXR_TOKEN_WCHAR_T_CHAR_CONST);
	string_len = this->string_len + 1;
	assert(sizeof(char32_t) == 4);
	utf32_string = realloc(utf32_string, string_len * 4);
	if (utf32_string == NULL)
		return ENOMEM;
	utf32_string[this->string_len] = cp;
	this->utf32_string = utf32_string;
	this->string_len = string_len;
	if (out_num_code_units)
		*out_num_code_units = 1;
	return ESUCCESS;
}

/*
 * The lexer is at the start delimiter.
 * As each code-point is scanned, convert it into the desginated exec-char-set
 * for this string and store that string as token->string.
 */
static
err_t lexer_lex_string_literal(struct lexer *this,
							   struct lexer_token *out)
{
	err_t err;
	int num_delimiters;
	struct lexer_token token;
	enum lexer_token_type type;
	enum char_const_escape_type esc_type;
	struct code_point cp;

	/*
	 * if invalid, then this is an char-string-literal.
	 * else this is a prefixed string-literal.
	 */
	if (out->type == LXR_TOKEN_INVALID)
		out->type = LXR_TOKEN_CHAR_STRING_LITERAL;
	type = out->type;

	token.type = LXR_TOKEN_INVALID;
	if (type == LXR_TOKEN_CHAR_STRING_LITERAL)
		token.type = LXR_TOKEN_INTEGER_CHAR_CONST;
	if (type == LXR_TOKEN_UTF_8_STRING_LITERAL)
		token.type = LXR_TOKEN_UTF_8_CHAR_CONST;
	if (type == LXR_TOKEN_UTF_16_STRING_LITERAL)
		token.type = LXR_TOKEN_UTF_16_CHAR_CONST;
	if (type == LXR_TOKEN_UTF_32_STRING_LITERAL)
		token.type = LXR_TOKEN_UTF_32_CHAR_CONST;
	if (type == LXR_TOKEN_WCHAR_T_STRING_LITERAL)
		token.type = LXR_TOKEN_WCHAR_T_CHAR_CONST;
	assert(token.type != LXR_TOKEN_INVALID);

	num_delimiters = 0;
	while (num_delimiters != 2) {
		err = lexer_peek_code_point(this, &cp);
		if (err)
			return err;

		if (cp.cp == '\"') {
			++num_delimiters;
			lexer_consume_code_point(this, &cp);	/* consume " */
			out->lex_size += cp.cp_size;	/* lex_size incl start/end " */
		} else {
			token.begin = this->position;
			token.lex_size = 0;
			err = lexer_lex_char_const_one(this, &esc_type, &token);
			if (err)
				return err;	/* invalid char in the string */
			out->lex_size = token.lex_size;
			cp.cp = token.code_point;
			/*
			 * For utf-16 char-const:
			 * If the char-const is produced through hex/oct esc-seq, its value
			 * is as specified by the hex/oct number (provided it is not larger
			 * than 0xffff.)
			 *
			 * If the char-const is produced directly or through ucn esc-seq,
			 * its value is equal to its iso/iec 10646 code-point value,
			 * provided the code-point value can be encoded as a single
			 * code-unit. Note that utf-16 code-unit is of 16-bits size. This
			 * limitation doesn't seem to apply when scanning a string, as both
			 * gcc and clang allow surrogate pairs.
			 *
			 * The checks for char16_t are provided separately in
			 * lexer_lex_string_literal and lexer_lex_char_const to account for
			 * this difference.
			 */
			/*
			 * All char-consts that survived are allowed, including surrogate
			 * pairs.
			 */
		}

		/*
		 * Convert the code-point into appropriate code-unit and store into the
		 * correct string field.
		 */
		err = lexer_token_store_code_point(out, cp.cp, NULL);
		if (err)
			return err;
	}
	return err;
}

/*
 * The lexer pos is at teh start delimiter.
 * For int-char-const, the start delimiter is not consumed.
 * For prefixed char-const, the start delimiter is consumed.
 */
static
err_t lexer_lex_char_const(struct lexer *this,
						   struct lexer_token *out)
{
	err_t err;
	char num_code_units;	/* c16 cus */
	struct code_point cp;
	enum char_const_escape_type esc_type;

	/*
	 * if invalid, then this is an integer-char-const.
	 * else this is a prefixed char-const.
	 */
	if (out->type == LXR_TOKEN_INVALID)
		out->type = LXR_TOKEN_INTEGER_CHAR_CONST;

	/*
	 * must be the starting delimiter. If it isn't, error.
	 * Delims are part of the char-const, however they do not play a role in
	 * determining the char-const's value.
	 */
	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	if (cp.cp != '\'')
		return EINVAL;
	lexer_consume_code_point(this, &cp);
	out->lex_size += cp.cp_size;
	err = lexer_token_store_code_point(out, cp.cp, NULL);
	if (err)
		return err;

	/*
	 * literal-encoding == mapping of chars of exec-char-set to
	 * the values in a char-const or string-literal.
	 *
	 * From cppreference.com:
	 * "The literal encoding is an implementation-defined mapping of the
	 * characters of the execution character set to the values in a character
	 * constant or string literal without encoding prefix."
	 *
	 * From: integer-char-const semantics (page 65, point # 11)
	 * the integer-char-const and char-string-literal are the bare char/string
	 * constants, without any prefix. The literal-encoding supported here is
	 * utf-8 only (same as src-char-set). IOW, the non-prefixed char/string
	 * constants are treated as if prefixed by u8.
	 * For this defintion, each byte is considered a value. Accordingly,
	 * characters that take multiple bytes are not allowed for now.
	 *
	 * It may be that the int-char-const has more than one valid characters.
	 * This too is not allowed. The program applies this restriction to
	 * u/U/u8/L char/string too.
	 *
	 * For utf-8 (char8_t):
	 *	a utf8-char-const not produced thru hex/oct esc-seq has value ==
	 *	the iso/iec 10646 code-point value, provided that the cp-value can be
	 *	encoded as a single utf-8 code-unit. For e.g. this fails the copyright
	 *	symbol, since the copy-right symbol needs two code-units 0xc2 0xa9
	 *
	 * This doesn't seem to be the responsibility of the preprocessor.
	 * But the preprocessor must deal with char-constants in #if/#elif and such
	 * constructs and it must at that time calc exec-char-set members. So this
	 * program processes chars here in the preprocessor.
	 */

	err = lexer_lex_char_const_one(this, &esc_type, out);
	if (!err)
		err = lexer_token_store_code_point(out, out->code_point,
										   &num_code_units);
	if (err)
		return err;

	/*
	 * Do not touch the out->code_point.
	 * It stores the value of the char-const.
	 */

	/*
	 * For utf-16 char-const:
	 * If the char-const is produced through hex/oct esc-seq, its value is as
	 * specified by the hex/oct number (provided it is not larger than 0xffff.)
	 *
	 * If the char-const is produced directly or through ucn esc-seq, its value
	 * is equal to its iso/iec 10646 code-point value, provided the code-point
	 * value can be encoded as a single code-unit. Note that utf-16 code-unit
	 * is of 16-bits size. This limitation doesn't seem to apply when scanning
	 * a string, as both gcc and clang allow surrogate pairs.
	 *
	 * The checks for char16_t are provided separately in
	 * lexer_lex_string_literal and lexer_lex_char_const to account for this
	 * difference.
	 */

	if (out->type == LXR_TOKEN_UTF_16_CHAR_CONST &&
		esc_type != CHAR_CONST_ESCAPE_OCT &&
		esc_type != CHAR_CONST_ESCAPE_HEX &&
		num_code_units > 1)
		return EINVAL;

	/* Next must be the ending delimiter. If it isn't, error. */
	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	if (cp.cp != '\'')
		return EINVAL;
	lexer_consume_code_point(this, &cp);
	out->lex_size += cp.cp_size;
	return lexer_token_store_code_point(out, cp.cp, NULL);
}
/*
 * Called when detecting a token starting with u/U/L. If what follows doesn't
 * describe a char-const or a string-literal, then the token is treated as a
 * potential identifier, since u/U/L are all in xid_start.
 */
static
err_t lexer_lex_prefixed_char_const_or_string_literal(struct lexer *this,
													  struct lexer_token *out)
{
	err_t err;
	struct code_point cp[3];
	struct lexer_position save[2];

	save[0] = this->position;	/* Save in case we have to rewind */

	err = lexer_peek_code_point(this, &cp[0]);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp[0]);	/* consume */
	out->lex_size += cp[0].cp_size;
	assert(cp[0].cp == 'u' || cp[0].cp == 'U' || cp[0].cp == 'L');

	err = lexer_peek_code_point(this, &cp[1]);
	if (err)
		goto lex_identifier;
	if (cp[0].cp == 'u' &&
		cp[1].cp != '8' && cp[1].cp != '\'' && cp[1].cp != '\"')
		goto lex_identifier;
	if (cp[0].cp == 'U' && cp[1].cp != '\'' && cp[1].cp != '\"')
		goto lex_identifier;
	if (cp[0].cp == 'L' && cp[1].cp != '\'' && cp[1].cp != '\"')
		goto lex_identifier;

	save[1] = this->position;	/* save the pos for ' or " delim. */
	lexer_consume_code_point(this, &cp[1]);	/* consume ' or " */
	out->lex_size += cp[1].cp_size;
	if (cp[0].cp == 'u' && cp[1].cp == '8') {
		err = lexer_peek_code_point(this, &cp[2]);
		if (err)
			goto lex_identifier;
		if (cp[2].cp != '\'' && cp[2].cp != '\"')
			goto lex_identifier;
		/* u8 is already at the start delim, so no restore of save[1]. */
	} else {
		/* rewind back to start delim for u/U/L */
		this->position = save[1];
	}

	if (cp[0].cp == 'u' && cp[1].cp == '8' && cp[2].cp == '\'')
		out->type = LXR_TOKEN_UTF_8_CHAR_CONST;
	if (cp[0].cp == 'u' && cp[1].cp == '\'')
		out->type = LXR_TOKEN_UTF_16_CHAR_CONST;
	if (cp[0].cp == 'U' && cp[1].cp == '\'')
		out->type = LXR_TOKEN_UTF_32_CHAR_CONST;
	if (cp[0].cp == 'L' && cp[1].cp == '\'')
		out->type = LXR_TOKEN_WCHAR_T_CHAR_CONST;

	if (cp[0].cp == 'u' && cp[1].cp == '8' && cp[2].cp == '\"')
		out->type = LXR_TOKEN_UTF_8_STRING_LITERAL;
	if (cp[0].cp == 'u' && cp[1].cp == '\"')
		out->type = LXR_TOKEN_UTF_16_STRING_LITERAL;
	if (cp[0].cp == 'U' && cp[1].cp == '\"')
		out->type = LXR_TOKEN_UTF_32_STRING_LITERAL;
	if (cp[0].cp == 'L' && cp[1].cp == '\"')
		out->type = LXR_TOKEN_WCHAR_T_STRING_LITERAL;

	assert(out->type != LXR_TOKEN_INVALID);

	if (out->type == LXR_TOKEN_UTF_8_CHAR_CONST ||
		out->type == LXR_TOKEN_UTF_16_CHAR_CONST ||
		out->type == LXR_TOKEN_UTF_32_CHAR_CONST ||
		out->type == LXR_TOKEN_WCHAR_T_CHAR_CONST)
		return lexer_lex_char_const(this, out);
	return lexer_lex_string_literal(this, out);
lex_identifier:
	out->lex_size = 0;
	this->position = save[0];
	return lexer_lex_identifier(this, out);
}
#endif
/*****************************************************************************/
/* > >= >> >>= */
static
err_t lexer_lex_greater_than(struct lexer *this,
							 struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_GREATER_THAN;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || (cp.cp != '>' && cp.cp != '='))
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_GREATER_THAN_EQUALS;
	out->lex_size += cp.cp_size;
	if (cp.cp == '=')
		return ESUCCESS;
	out->type = LXR_TOKEN_SHIFT_RIGHT;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '=')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_SHIFT_RIGHT_ASSIGN;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}

/* < <= << <<=. Digraphs <: and others are not supported. */
static
err_t lexer_lex_less_than(struct lexer *this,
						  struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_LESS_THAN;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || (cp.cp != '<' && cp.cp != '='))
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_LESS_THAN_EQUALS;
	out->lex_size += cp.cp_size;
	if (cp.cp == '=')
		return ESUCCESS;
	out->type = LXR_TOKEN_SHIFT_LEFT;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '=')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_SHIFT_LEFT_ASSIGN;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}
/*****************************************************************************/
/* & && &= */
static
err_t lexer_lex_and(struct lexer *this,
					struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_BITWISE_AND;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || (cp.cp != '&' && cp.cp != '='))
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_BITWISE_AND_ASSIGN;
	out->lex_size += cp.cp_size;
	if (cp.cp == '=')
		return ESUCCESS;
	out->type = LXR_TOKEN_LOGICAL_AND;
	return ESUCCESS;
}

/* | || |= */
static
err_t lexer_lex_or(struct lexer *this,
				   struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_BITWISE_OR;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || (cp.cp != '|' && cp.cp != '='))
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_BITWISE_OR_ASSIGN;
	out->lex_size += cp.cp_size;
	if (cp.cp == '=')
		return ESUCCESS;
	out->type = LXR_TOKEN_LOGICAL_OR;
	return ESUCCESS;
}
/*****************************************************************************/
/* - -- -= */
static
err_t lexer_lex_minus(struct lexer *this,
					  struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_MINUS;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || (cp.cp != '-' && cp.cp != '='))
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_MINUS_ASSIGN;
	out->lex_size += cp.cp_size;
	if (cp.cp == '=')
		return ESUCCESS;
	out->type = LXR_TOKEN_DECR;
	return ESUCCESS;
}

/* + ++ += */
static
err_t lexer_lex_plus(struct lexer *this,
					 struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_PLUS;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || (cp.cp != '+' && cp.cp != '='))
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_PLUS_ASSIGN;
	out->lex_size += cp.cp_size;
	if (cp.cp == '=')
		return ESUCCESS;
	out->type = LXR_TOKEN_INCR;
	return ESUCCESS;
}
/*****************************************************************************/
static
err_t lexer_lex_single_char(struct lexer *this,
							struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	assert(cp.cp_size == 1);
	out->lex_size += cp.cp_size;

	switch (cp.cp) {
	case '(': out->type = LXR_TOKEN_LEFT_PAREN;		break;
	case '[': out->type = LXR_TOKEN_LEFT_BRACKET;	break;
	case '{': out->type = LXR_TOKEN_LEFT_BRACE;		break;
	case ')': out->type = LXR_TOKEN_RIGHT_PAREN;	break;
	case ']': out->type = LXR_TOKEN_RIGHT_BRACKET;	break;
	case '}': out->type = LXR_TOKEN_RIGHT_BRACE;	break;

	case '?': out->type = LXR_TOKEN_QUESTION_MARK;	break;
	case ';': out->type = LXR_TOKEN_SEMI_COLON;		break;
	case '~': out->type = LXR_TOKEN_TILDE;			break;
	case ',': out->type = LXR_TOKEN_COMMA;			break;
	case '@': out->type = LXR_TOKEN_AT;				break;
	case '\\': out->type = LXR_TOKEN_BACK_SLASH;	break;
	default: return EINVAL;
	}
	return ESUCCESS;
}
/*****************************************************************************/
/* ! != */
static
err_t lexer_lex_not(struct lexer *this,
					struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_EXCLAMATION_MARK;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '=')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_NOT_EQUALS;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}

/* / /= */
static
err_t lexer_lex_div(struct lexer *this,
					struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_DIV;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '=')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_DIV_ASSIGN;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}

/* ^ ^= */
static
err_t lexer_lex_xor(struct lexer *this,
					struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_BITWISE_XOR;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '=')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_BITWISE_XOR_ASSIGN;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}
/*****************************************************************************/
/* % %= */
static
err_t lexer_lex_mod(struct lexer *this,
					struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_MOD;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '=')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_MOD_ASSIGN;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}
/*****************************************************************************/
/* * *= */
static
err_t lexer_lex_mul(struct lexer *this,
					struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_MUL;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '=')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_MUL_ASSIGN;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}
/*****************************************************************************/
/* : :: */
static
err_t lexer_lex_colon(struct lexer *this,
					  struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_COLON;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != ':')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_DOUBLE_COLON;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}
/*****************************************************************************/
/* = == */
static
err_t lexer_lex_equals(struct lexer *this,
					   struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_ASSIGN;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '=')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_EQUALS;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}
/*****************************************************************************/
/* # ## */
static
err_t lexer_lex_hash(struct lexer *this,
					 struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_HASH;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '#')
		return ESUCCESS;
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_DOUBLE_HASH;
	out->lex_size += cp.cp_size;
	return ESUCCESS;
}
/*****************************************************************************/
static
err_t lexer_lex_identifier(struct lexer *this,
						   struct lexer_token *out)
{
	int i;
	const char *str;
	err_t err;
	struct code_point cp;

	/* Read the xid_start */
	err =  lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	assert(is_xid_start(cp.cp));
	lexer_consume_code_point(this, &cp);
	out->type = LXR_TOKEN_IDENTIFIER;
	out->lex_size += cp.cp_size;

	while (true) {
		err = lexer_peek_code_point(this, &cp);
		if (err == EOF) {	/* EOF is allowed */
			err = ESUCCESS;
			break;
		}
		if (err)
			return err;
		if (cp.cp == '\\') {
			lexer_consume_code_point(this, &cp);
			err = lexer_peek_code_point(this, &cp);
			if (err)
				return err;
			if (cp.cp != 'u' && cp.cp != 'U')
				return EINVAL;
			if (cp.cp == 'u')
				err = lexer_lex_ucn_escape_char(this, 4, out);
			else
				err = lexer_lex_ucn_escape_char(this, 8, out);
			if (err)
				return err;
			cp.cp = out->value;
			cp.cp_size = 0;
			/*
			 * Not acutally cp_size, but helps in the out->lex_size += ...
			 * statement below. the lex_size was already incremented by
			 * the ucn functions above.
			 */
		}
		if (!is_xid_continue(cp.cp))
			break;
		out->lex_size += cp.cp_size;
		lexer_consume_code_point(this, &cp);
	}
	out->type = LXR_TOKEN_IDENTIFIER;
	str = this->buffer + this->begin.lex_pos;
	for (i = 0; i < (int)ARRAY_SIZE(g_key_words); ++i) {
		/* char8_t -> char pointer */
		if (strncmp(g_key_words[i], str, out->lex_size))
			continue;
		if (g_key_words[i][out->lex_size] != NULL_CHAR)
			continue;
		out->type += i + 1;
		return ESUCCESS;
	}
	return ESUCCESS;
}
/*****************************************************************************/
static
err_t lexer_lex_number(struct lexer *this,
					   struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	/*
	 * If lex_size == 0, we are just beginning to scan a number.
	 * If lex_size != 0, and we arrive here, it must be to scan more
	 * pp-number constructs.
	 */

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;

	/* A number begins with a digit, or a . digit */
	if (out->lex_size == 0) {
		out->type = LXR_TOKEN_NUMBER;

		lexer_consume_code_point(this, &cp);
		out->lex_size += cp.cp_size;

		if (cp.cp == '.') {
			/* There must be a digit next. */
			err = lexer_peek_code_point(this, &cp);
			if (err)
				return err;
			if (!is_dec_digit(cp.cp))
				return EINVAL;
			lexer_consume_code_point(this, &cp);
			out->lex_size += cp.cp_size;
		} else {
			assert(is_dec_digit(cp.cp));
		}
	} else {
		/* we must have xid_continue or . or ' or e/E or p/P */
		lexer_consume_code_point(this, &cp);
		out->lex_size += cp.cp_size;

		if (cp.cp == '.') {
			/* do nothing */
		} else if (cp.cp == '\'') {
			/* digit/non-digit */
			err = lexer_peek_code_point(this, &cp);
			if (err)
				return err;
			if (is_a_z(cp.cp) ||
				is_A_Z(cp.cp) ||
				is_dec_digit(cp.cp) ||
				cp.cp == '_') {
				lexer_consume_code_point(this, &cp);
				out->lex_size += cp.cp_size;
			} else {
				return EINVAL;
			}
		} else if (cp.cp == 'e' ||
				   cp.cp == 'E' ||
				   cp.cp == 'p' ||
				   cp.cp == 'P') {
			/* A sign may follow. If not, treat e/E/p/P as xid-continue. */
			err = lexer_peek_code_point(this, &cp);
			if (err)
				return err;
			if (cp.cp == '+' || cp.cp == '-') {
				lexer_consume_code_point(this, &cp);
				out->lex_size += cp.cp_size;
			} else {
				/* e/E/p/P are still valid as xid-continue; */
			}
		} else {
			assert(is_xid_continue(cp.cp));
		}
	}

	/* Now check for constructs that can follow a pp-number */
	err = lexer_peek_code_point(this, &cp);
	if (err == EOF)
		return ESUCCESS;
	else if (err)
		return err;

	if (cp.cp == '.' || cp.cp == '\'' ||
		cp.cp == 'e' || cp.cp == 'E' ||
		cp.cp == 'p' || cp.cp == 'P' ||
		is_xid_continue(cp.cp))
		return lexer_lex_number(this, out);
	return err;
}
/*****************************************************************************/
static
err_t lexer_lex_dot(struct lexer *this,
					struct lexer_token *out)
{
	err_t err;
	struct lexer_position after_one_dot;
	struct code_point cp;

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;
	lexer_consume_code_point(this, &cp);	/* first dot consumed */
	after_one_dot = this->position;
	out->type = LXR_TOKEN_DOT;
	out->lex_size += cp.cp_size;

	err = lexer_peek_code_point(this, &cp);
	if (err || (cp.cp != '.' && !is_dec_digit(cp.cp)))
		return ESUCCESS;
	if (is_dec_digit(cp.cp)) {
		/* Reset the output and position for scanning a pp-number */
		lexer_token_init(out);
		this->position = this->begin;
		return lexer_lex_number(this, out);
	}
	lexer_consume_code_point(this, &cp);	/* second dot consumed */

	/* We have seen 2 dots; but .. isn't valid. token is still one dot. */
	err = lexer_peek_code_point(this, &cp);
	if (err || cp.cp != '.') {
		/* no third dot; the final token is a single dot */
		this->position = after_one_dot;
		return ESUCCESS;
	}
	lexer_consume_code_point(this, &cp);	/* third dot consumed */
	out->type = LXR_TOKEN_ELLIPSIS;
	out->lex_size += 2 * cp.cp_size;
	return ESUCCESS;
}
/*****************************************************************************/
static
err_t _lexer_lex_token(struct lexer *this,
					   struct lexer_token *out)
{
	err_t err;
	struct code_point cp;

	/* TODO: \U00030000argc is a valid identifier */

	err = lexer_peek_code_point(this, &cp);
	if (err)
		return err;

	if (out->is_first)
		printf("%s: line begins\n", __func__);
	printf("%s: pos %ld, file (%ld,%ld), ws? %d, 1st? %d, cp %c\n", __func__,
		   this->begin.lex_pos,
		   this->begin.file_row + 1,
		   this->begin.file_col + 1,
		   out->has_white_space,
		   out->is_first,
		   cp.cp);

	/* single-char tokens */
	if (cp.cp == '(' || cp.cp == ')' ||
		cp.cp == '[' || cp.cp == ']' ||
		cp.cp == '{' || cp.cp == '}' ||
		cp.cp == '?' || cp.cp == ';' ||	cp.cp == '~' ||	cp.cp == ',' ||
		cp.cp == '@' || cp.cp == '\\' )
		err = lexer_lex_single_char(this, out);
	else if (cp.cp == '#')
		err = lexer_lex_hash(this, out);
	else if (cp.cp == '=')
		err = lexer_lex_equals(this, out);
	else if (cp.cp == '%')
		err = lexer_lex_mod(this, out);
	else if (cp.cp == '!')
		err = lexer_lex_not(this, out);
	else if (cp.cp == '/')
		err = lexer_lex_div(this, out);
	else if (cp.cp == '^')
		err = lexer_lex_xor(this, out);
	else if (cp.cp == ':')
		err = lexer_lex_colon(this, out);
	else if (cp.cp == '<')
		err = lexer_lex_less_than(this, out);
	else if (cp.cp == '>')
		err = lexer_lex_greater_than(this, out);
	else if (cp.cp == '|')
		err = lexer_lex_or(this, out);
	else if (cp.cp == '&')
		err = lexer_lex_and(this, out);
	else if (cp.cp == '+')
		err = lexer_lex_plus(this, out);
	else if (cp.cp == '-')
		err = lexer_lex_minus(this, out);
	else if (cp.cp == '*')
		err = lexer_lex_mul(this, out);
	else if (cp.cp == '.')
		err = lexer_lex_dot(this, out);
#if 0
	else if (cp.cp == 'u' || cp.cp == 'U' || cp.cp == 'L')
		err = lexer_lex_prefixed_char_const_or_string_literal(this, out);
	else if (cp.cp == '\'')
		err = lexer_lex_char_const(this, out);
	else if (cp.cp == '\"')
		err = lexer_lex_string_literal(this, out);
#endif
	else if (is_xid_start(cp.cp))
		err = lexer_lex_identifier(this, out);
	else if (is_dec_digit(cp.cp))
		err = lexer_lex_number(this, out);
	else
		goto unsup_cp;
	if (!err && lexer_token_source(out) == NULL)
		err = lexer_build_source(this, out);
	if (!err)
		lexer_token_print(out, &this->begin, __func__);
	return err;
unsup_cp:
	printf("%s: unsupported cp: 0x%x\n", __func__, cp.cp);
	exit(0);
}
/*****************************************************************************/
/*
 * sets out to the # of white-spaces before a non-ws cp.
 * returns true if the cp is also the first non-ws cp on the line.
 */
static
bool lexer_skip_white_spaces(struct lexer *this,
							 int *out)
{
	err_t err;
	bool is_first;
	int num_white_spaces;
	struct code_point cp;
	struct lexer_position save;

	*out = num_white_spaces = 0;
	is_first = this->position.lex_pos == 0;

	/* count white-spaces on the line. \n resets the count. */
	while (true) {
		err = lexer_peek_code_point(this, &cp);
		if (err)
			break;	/* let the token scanner handle the error */

		if (cp.cp == '\n') {
			is_first = true;
			num_white_spaces = 0;
			lexer_consume_code_point(this, &cp);
			continue;
		}

		/* non-lf white-spaces */
		if (is_white_space(cp.cp)) {
			++num_white_spaces;
			lexer_consume_code_point(this, &cp);
			continue;
		}

		/* Don't consume yet. */
		if (cp.cp != '/')
			break;

		/* check for comments */
		assert(this->position.lex_pos == cp.begin.lex_pos);
		save = cp.begin;	/* Save position of slash */
		lexer_consume_code_point(this, &cp);
		err = lexer_peek_code_point(this, &cp);
		if (err || (cp.cp != '*' && cp.cp != '/')) {
			/* switch back the scanner's position to slash. */
			this->position = save;
			break;
		}

		/* Consume the star or the slash */
		lexer_consume_code_point(this, &cp);

		/* Each comment counts as one white-space */
		if (cp.cp == '/')
			lexer_skip_single_line_comment(this);
		else
			lexer_skip_multi_line_comment(this);
		++num_white_spaces;
	}
	*out = num_white_spaces;
	return is_first;
}

err_t lexer_lex_token(struct lexer *this,
					  struct lexer_token **out)
{
	int num_white_spaces;
	struct lexer_token *token;

	token = malloc(sizeof(*token));
	if (token == NULL)
		return ENOMEM;

	*out = token;
	lexer_token_init(token);
	lexer_token_ref(token);	/* lexer's ref count */
	token->is_first = lexer_skip_white_spaces(this, &num_white_spaces);
	/* we change >1 spaces to 1. This may affect #include paths. */
	token->has_white_space = num_white_spaces ? 1 : 0;
	this->begin = this->position;
	return _lexer_lex_token(this, token);
}
