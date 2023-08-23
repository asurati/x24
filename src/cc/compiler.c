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
/*****************************************************************************/
void cc_token_delete(void *p)
{
	struct cc_token *this = p;
	free((void *)this->string);
	free(this);
}
/*****************************************************************************/
err_t compiler_new(const char *path,
				   struct compiler **out)
{
	err_t err;
	int fd, ret;
	const char *buffer;
	size_t size;
	struct compiler *this;
	struct stat stat;

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
	cc_token_stream_init(&this->stream, buffer, size);
	*out = this;
	return ESUCCESS;
err1:
	close(fd);
err0:
	return err;
}

err_t compiler_delete(struct compiler *this)
{
	assert(this);
	munmap((void *)this->stream.buffer, this->stream.buffer_size);
	close(this->cpp_tokens_fd);
	unlink(this->cpp_tokens_path);
	free((void *)this->cpp_tokens_path);
	cc_token_stream_empty(&this->stream);
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
	if (i == str_len) {
		if (num_digits == 0 && dot_seen == false)
			return EINVAL;
		return ESUCCESS;
	}

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

		dot_seen = true;
		num_digits = 0;	/* Can't have seperator imm. follow the dot */
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

	/* float-suffix without exp */
	if (str[i] == 'd' || str[i] == 'f' || str[i] == 'l' ||
		str[i] == 'D' || str[i] == 'F' || str[i] == 'L')
		goto floating_suffix;

	return EINVAL;
exponent:
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
		return EINVAL;	/* There must be digits after exp. */
	/* fall-thru */
floating_suffix:
	if (i == str_len)
		return ESUCCESS;
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
done:
	if (i != str_len)
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
err_t cc_token_stream_convert(struct cc_token_stream *this,
							  struct cc_token **out)
{
	err_t err;
	struct cc_token *token;
	char *src;
	size_t src_len;
	enum cc_token_type type;	/* lxr_token_type == cc_token_type */
	static size_t index = 0;

	assert(sizeof(type) == 4);
	memcpy(&type, &this->buffer[index], sizeof(type));
	index += sizeof(type);
	memcpy(&src_len, &this->buffer[index], sizeof(src_len));
	index += sizeof(src_len);
	src = NULL;
	if (src_len) {
		src = malloc(src_len + 1);
		if (src == NULL)
			return ENOMEM;
		src[src_len] = 0;
		memcpy(src, &this->buffer[index], src_len);
		index += src_len;
	}
	token = malloc(sizeof(*token));
	if (token == NULL)
		return ENOMEM;
	token->type = type;
	token->string = src;
	token->string_len = src_len;

	err = ESUCCESS;
	if (type == CC_TOKEN_NUMBER)
		err = cc_token_convert_number(token);
	if (!err)
		*out = token;
	return err;
}

static
err_t cc_token_stream_peek_head(struct cc_token_stream *this,
								struct cc_token **out)
{
	err_t err;

	if (!cc_token_stream_is_empty(this)) {
		*out = queue_peek_head(&this->tokens);
		return ESUCCESS;
	}

	/* Not reading from the cpp_tokens file? EOF */
	if (this->buffer == NULL)
		return EOF;
	err = cc_token_stream_convert(this, out);
	if (!err)
		err = queue_add_tail(&this->tokens, *out);
	return err;
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
	token = queue_remove_head(&this->tokens);
	assert(token == *out);
	return err;
}
/*****************************************************************************/
err_t compiler_compile(struct compiler *this)
{
	err_t err;
	struct cc_token *token;

	err = cc_token_stream_remove_head(&this->stream, &token);
	return err;
}
