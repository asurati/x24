/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include "scanner.h"
#include <inc/unicode.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>

static
void macro_delete(void *p);
static
err_t scanner_scan_file(struct scanner *this,
						const char *path);
static
const struct macro *scanner_find_macro(const struct scanner *this,
									   const char *ident);
static
err_t cpp_token_stream_remove_head(struct cpp_token_stream *this,
								   struct cpp_token **out);
static
err_t scanner_expand_argument(struct scanner *this,
							  const struct queue *arg,
							  struct queue *out);
static
err_t scanner_process_one(struct scanner *this,
						  struct queue *mstk,
						  struct cpp_token_stream *stream,
						  struct queue *out);
static
err_t scanner_arg_substitution(struct scanner *this,
							   const struct macro *macro,
							   const struct queue *args,
							   const struct queue *exp_args,
							   const int num_args,
							   struct queue *repl,
							   struct queue *out);
static
err_t scanner_arg_substitution_one(struct scanner *this,
								   const struct macro *macro,
								   const struct queue *args,
								   const struct queue *exp_args,
								   const int num_args,
								   const bool is_pasting,
								   struct queue *repl,
								   struct queue *out);
#if 0
static
int __attribute__((noinline)) break_point_func() {return 0;}
#endif
/*****************************************************************************/
#if 0
void char_string_print(const char *str,
					   const int len)
{
	int i;

	assert(str);
	for (i = 0; i < len; ++i)
		printf("%c", str[i]);
}
#endif
/*****************************************************************************/
err_t scanner_new(struct scanner **out)
{
	int i;
	err_t err;
	struct scanner *this;

	this = malloc(sizeof(*this));
	if (this == NULL)
		return ENOMEM;

	array_init(&this->macros, macro_delete);
	stack_init(&this->cistk);

	this->cpp_tokens_path = NULL;
	this->cpp_tokens_fd = -1;
	this->is_running_predefined_macros = true;

	this->include_paths[0] = "/usr/include";
	this->include_paths[1] = "/usr/lib/gcc/x86_64-pc-linux-gnu/13.2.1/include";
	this->include_paths[2] = NULL;
	this->include_paths[3] = NULL;
	for (i = 0; i < 4; ++i) {
		if (this->include_paths[i] == NULL)
			continue;
		this->include_path_lens[i] = strlen(this->include_paths[i]);
	}

	/* Open a temp file for serializing cpp_tokens */
	err = mkstemp(&this->cpp_tokens_fd, &this->cpp_tokens_path);
	if (err)
		return err;
	*out = this;
	return ESUCCESS;
}

err_t scanner_delete(struct scanner *this)
{
	assert(this);

	close(this->cpp_tokens_fd);
	/*unlink(this->cpp_tokens_path);*/
	free((void *)this->cpp_tokens_path);

	assert(stack_num_entries(&this->cistk) == 0);
	array_empty(&this->macros);
	free(this);
	return ESUCCESS;
}

const char *scanner_cpp_tokens_path(const struct scanner *this)
{
	return this->cpp_tokens_path;
}
/*****************************************************************************/
static
int scanner_find_macro_index(const struct scanner *this,
							 const char *ident)
{
	int i;
	const char *name[2];
	const struct macro *macro;

	name[0] = ident;
	array_for_each(&this->macros, i, macro) {
		if (macro == NULL)
			continue;
		name[1] = cpp_token_resolved(macro->identifier);
		if (!strcmp(name[0], name[1]))
			return i;
	}
	return EOF;
}

static
const struct macro *scanner_find_macro(const struct scanner *this,
									   const char *ident)
{
	int i = scanner_find_macro_index(this, ident);
	if (i < 0)
		return NULL;
	return array_peek_entry(&this->macros, i);
}
/*****************************************************************************/
/* During deletion, take care to not free static mem */
static
void cpp_token_delete(void *p)
{
	struct cpp_token *this = p;
	lexer_token_deref(this->base);
	free(this);
}

/* Do not incr. ref-count on base here */
static
err_t cpp_token_new(struct lexer_token *base,
					struct cpp_token **out)
{
	struct cpp_token *this;

	this = malloc(sizeof(*this));
	if (this == NULL)
		return ENOMEM;

	this->base = base;	/* Move lexer's ref-count into token */
	this->is_marked = false;
	this->is_first = base->is_first;
	this->has_white_space = base->has_white_space;
	*out = this;
	return ESUCCESS;
}

static
err_t cpp_token_copy(struct cpp_token *this,
					 struct cpp_token **out)
{
	err_t err;
	struct cpp_token *token;

	err = cpp_token_new(this->base, &token);
	if (err)
		return err;
	lexer_token_ref(this->base);	/* On copy, incr. the ref-count */
	token->is_marked = this->is_marked;
	token->is_first = this->is_first;
	token->has_white_space = this->has_white_space;
	*out = token;
	return err;
}

/* has_white_space arrives from the token that num is supposed to replace. */
err_t cpp_token_new_number(const int num,
						   const bool has_white_space,
						   const bool is_first,
						   struct cpp_token **out)
{
	err_t err;
	int num_digits, n, len;
	char *str;
	struct lexer *lexer;
	struct cpp_token_stream stream;
	struct cpp_token *this;

	assert(num >= 0);	/* For now */

	num_digits = 0;
	n = num;
	while (n) {
		++num_digits;
		n /= 10;
	}
	if (num_digits == 0) {
		assert(num == 0);
		num_digits = 1;
	}

	len = num_digits + 1;	/* nl */
	str = malloc(len + 1);	/* nul */
	if (str == NULL)
		return ENOMEM;
	sprintf(str, "%d", num);
	str[len - 1] = '\n';
	str[len] = NULL_CHAR;

	err = lexer_new(NULL, str, len, &lexer);
	if (err)
		return err;
	cpp_token_stream_init(&stream, lexer);
	err = cpp_token_stream_remove_head(&stream, &this);
	if (err)
		return err;
	assert(cpp_token_type(this) == LXR_TOKEN_NUMBER);
	assert(cpp_token_stream_is_empty(&stream));
	lexer_delete(lexer);

	this->is_first = is_first;
	this->has_white_space = has_white_space;
	*out = this;
	return err;
}

err_t cpp_token_new_place_marker(const bool has_white_space,
								 const bool is_first,
								 struct cpp_token **out)
{
	struct lexer_token *base;

	base = malloc(sizeof(*base));
	if (base == NULL)
		return ENOMEM;
	lexer_token_init(base);	/* ref-count == 1 */
	base->type = LXR_TOKEN_PLACE_MARKER;
	base->has_white_space = has_white_space;
	base->is_first = is_first;
	return cpp_token_new(base, out);
}
/*****************************************************************************/
/* out init by the caller */
static
err_t cpp_tokens_copy(const struct queue *this,
					  struct queue *out)
{
	int i;
	err_t err;
	struct cpp_token *t;
	struct cpp_token *token;

	queue_for_each(this, i, t) {
		err = cpp_token_copy(t, &token);
		if (!err)
			err = queue_add_tail(out, token);
		if (err)
			return err;
	}
	assert(i == queue_num_entries(this));
	return ESUCCESS;
}

/*
 * TODO:
 * A marker that is removed donates its npws to the token that follows.
 */
static
err_t cpp_tokens_remove_place_markers(struct queue *this)
{
	err_t err;
	struct cpp_token *token;
	struct queue out;

	queue_init(&out, cpp_token_delete);
	queue_for_each_with_rem(this, token) {
		if (cpp_token_type(token) == LXR_TOKEN_PLACE_MARKER) {
			cpp_token_delete(token);
			continue;
		}
		err = queue_add_tail(&out, token);
		if (err)
			return err;
	}
	assert(queue_is_empty(this));
	return queue_move(&out, this);
}
/*****************************************************************************/
/* no ref change on base when token is placed/removed from queues, etc. */
static
err_t cpp_token_stream_peek_head(struct cpp_token_stream *this,
								 struct cpp_token **out)
{
	err_t err;
	struct lexer_token *base;
	struct lexer *lexer;
	struct cpp_token *token;

	if (!cpp_token_stream_is_empty(this)) {
		*out = queue_peek_head(&this->tokens);
		return ESUCCESS;
	}

	/* not having a lexer and with empty tokens, implies end-of-stream */
	lexer = this->lexer;
	if (lexer == NULL)
		return EOF;

	err = lexer_lex_token(lexer, &base);	/* ref-count is 1 */
	if (!err)
		err = cpp_token_new(base, &token);	/* move lexer's ref */
	if (!err)
		err = queue_add_tail(&this->tokens, token);
	if (!err)
		*out = token;
	return err;
}

static
err_t cpp_token_stream_remove_head(struct cpp_token_stream *this,
								   struct cpp_token **out)
{
	err_t err;

	err = cpp_token_stream_peek_head(this, out);
	assert(err || queue_remove_head(&this->tokens) == *out);
	return err;
}
/*****************************************************************************/
static
void macro_delete(void *p)
{
	struct macro *this = p;
	cpp_token_delete(this->identifier);
	queue_empty(&this->parameters);
	queue_empty(&this->replacement_list);
	free(this);
}

/* Called after verifying that identifiers are equal */
static
bool macro_are_identical(const struct macro *m[2])
{
	int i;
	int len[2], num_params[2], num_repl_tokens[2];
	const char *str[2];
	bool ws[2];
	const struct cpp_token *token;

	if (m[0]->is_function_like != m[1]->is_function_like)
		return false;
	if (m[0]->is_variadic != m[1]->is_variadic)
		return false;

	num_params[0] = queue_num_entries(&m[0]->parameters);
	num_params[1] = queue_num_entries(&m[1]->parameters);
	if (num_params[0] != num_params[1])
		return false;

	num_repl_tokens[0] = queue_num_entries(&m[0]->replacement_list);
	num_repl_tokens[1] = queue_num_entries(&m[1]->replacement_list);
	if (num_repl_tokens[0] != num_repl_tokens[1])
		return false;

	/* Check param-names */
	for (i = 0; i < num_params[0]; ++i) {
		token = queue_peek_entry(&m[0]->parameters, i);
		len[0] = cpp_token_resolved_length(token);
		str[0] = cpp_token_resolved(token);
		token = queue_peek_entry(&m[1]->parameters, i);
		len[1] = cpp_token_resolved_length(token);
		str[1] = cpp_token_resolved(token);
		if (len[0] != len[1] || strcmp(str[0], str[1]))
			return false;
	}

	/* Check repl-lists */
	for (i = 0; i < num_repl_tokens[0]; ++i) {
		token = queue_peek_entry(&m[0]->replacement_list, i);
		len[0] = cpp_token_resolved_length(token);
		str[0] = cpp_token_resolved(token);
		ws[0] = cpp_token_has_white_space(token);
		token = queue_peek_entry(&m[1]->replacement_list, i);
		len[1] = cpp_token_resolved_length(token);
		str[1] = cpp_token_resolved(token);
		ws[1] = cpp_token_has_white_space(token);
		if (ws[0] != ws[1] || len[0] != len[1] || strcmp(str[0], str[1]))
			return false;
	}
	return true;
}

static
err_t macro_scan_parameters(struct macro *this,
							struct queue *line)
{
	err_t err;
	int len[2], num_params, i, j;
	bool was_prev_comma, closed;
	const char *str[2];
	struct cpp_token *token;
	enum lexer_token_type type;

	/* There should be at least a closing paren */
	if (queue_is_empty(line))
		return EINVAL;

	/* No parameters */
	token = queue_peek_head(line);
	type = cpp_token_type(token);
	if (type == LXR_TOKEN_RIGHT_PAREN) {
		queue_delete_head(line);
		return ESUCCESS;
	}

	was_prev_comma = true;	/* so that we start with ident */
	closed = false;
	while (!queue_is_empty(line)) {
		token = queue_remove_head(line);
		type = cpp_token_type(token);

		if (this->is_variadic && type != LXR_TOKEN_RIGHT_PAREN)
			return EINVAL;

		/* End of params */
		if (type == LXR_TOKEN_RIGHT_PAREN) {
			/* the closing right-paren should not be after a comma */
			if (was_prev_comma)
				return EINVAL;
			closed = true;
			cpp_token_delete(token);
			break;
		}

		if (was_prev_comma) {
			was_prev_comma = false;

			/* after a comma, we expect either ident or ellipsis */
			if (!cpp_token_is_identifier(token) &&
				type != LXR_TOKEN_ELLIPSIS)
				return EINVAL;

			/* can't have va-opts and va-args as parameters */
			if (type == LXR_TOKEN_VA_ARGS ||
				type == LXR_TOKEN_VA_OPT)
				return EINVAL;

			assert(this->is_variadic == false);
			if (type == LXR_TOKEN_ELLIPSIS)
				this->is_variadic = true;

			/* token's ref on base remains intact. */
			err = queue_add_tail(&this->parameters, token);
			if (err)
				return err;
			continue;
		}

		assert(was_prev_comma == false);
		if (type != LXR_TOKEN_COMMA)
			return EINVAL;

		cpp_token_delete(token);
		/* param-separating comma. */
		was_prev_comma = true;
	}
	if (closed == false)	/* closing right-paren not found */
		return EINVAL;

	/* each parameter must be uniquely named among the parameters. */
	num_params = queue_num_entries(&this->parameters);
	assert(num_params);

	/* No need to check ellipsis. */
	if (this->is_variadic)
		--num_params;

	for (i = 0; i < num_params - 1; ++i) {
		token = queue_peek_entry(&this->parameters, i);
		len[0] = cpp_token_resolved_length(token);
		str[0] = cpp_token_resolved(token);
		for (j = i + 1; j < num_params; ++j) {
			token = queue_peek_entry(&this->parameters, j);
			len[1] = cpp_token_resolved_length(token);
			str[1] = cpp_token_resolved(token);
			if (len[0] != len[1] || strcmp(str[0], str[1]))
				continue;
			return EINVAL;
		}
	}
	return ESUCCESS;
}

static
err_t macro_scan_replacement_list(struct macro *this,
								  struct queue *line)
{
	bool is_first;
	struct cpp_token *token;
	enum lexer_token_type type;

	/* Empty is allowed */
	if (queue_is_empty(line))
		return ESUCCESS;

	is_first = true;
	type = LXR_TOKEN_INVALID;
	while (!queue_is_empty(line)) {
		token = queue_remove_head(line);
		type = cpp_token_type(token);

		/* token-pasting cannot be the first token in a repl-list. */
		if (is_first && type == LXR_TOKEN_DOUBLE_HASH)
			return EINVAL;

		/*
		 * obj-like can't have va-args and va-opts in its repl-list.
		 * non-variadic func-like too can't have them.
		 */
		if (type == LXR_TOKEN_VA_ARGS || type == LXR_TOKEN_VA_OPT) {
			if (!this->is_function_like)
				return EINVAL;
			if (!this->is_variadic)
				return EINVAL;
		}

		/*
		 * obj-like can have # its repl-list, but there it isn't used as a
		 * stringizing operator.
		 */
#if 0
		if (type == LXR_TOKEN_HASH && !this->is_function_like)
			return EINVAL;
#endif

		/*
		 * The white-space preceeding the first token in the replacement-list
		 * is immaterial. >1 white-space between tokens in the repl-list is
		 * replaced by a signle space.
		 */
		if (is_first)
			token->has_white_space = false;
		queue_add_tail(&this->replacement_list, token);
		is_first = false;
	}
	/* token-pasting cannot be the last token in a repl-list. */
	if (type == LXR_TOKEN_DOUBLE_HASH)
		return EINVAL;
	return ESUCCESS;
}

static
int macro_find_parameter(const struct macro *this,
						 const char *ident)
{
	int i;
	const char *name[2];
	const struct cpp_token *token;

	name[0] = ident;
	queue_for_each(&this->parameters, i, token) {
		name[1] = cpp_token_resolved(token);
		if (!strcmp(name[0], name[1]))
			return i;
	}
	return EOF;
}
/*****************************************************************************/
static
err_t scanner_include_hseq(struct scanner *this,
						   const char *name)
{
	err_t err;
	int i, fd, name_len;
	char *path;

	err = ENOENT;
	name_len = strlen(name);
	for (i = 0; i < 4; ++i) {
		if (this->include_paths[i] == NULL)
			continue;

		path = malloc(name_len + this->include_path_lens[i] + 1 + 1);
		if (path == NULL)
			return ENOMEM;

		strcpy(path, this->include_paths[i]);
		strcat(path, "/");
		strcat(path, name);

		/* Check if the file exists */
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			free(path);
			continue;
		}
		err = scanner_scan_file(this, path);
		close(fd);
		free(path);
		break;
	}
	return err;
}

static
err_t scanner_include_qseq(struct scanner *this,
						   const char *dir_path,
						   const char *in_name)
{
	err_t err;
	int path_len, fd, name_len;
	char *path;
	char *name;

	/* file-name is a cpp string - it contains delimiters. Strip them */
	name_len = strlen(in_name);
	if (name_len <= 2)
		return EINVAL;	/* Can't have just "" */

	name_len -= 2; /* Strip the delimiters */
	name = malloc(name_len + 1);
	if (name == NULL)
		return ENOMEM;
	strncpy(name, &in_name[1], name_len);
	name[name_len] = 0;	/* To silence valgrind uninit-use warning. */

	path_len = strlen(dir_path) + name_len + 1;	/* + 1 for / */
	path = malloc(path_len + 1);
	if (path == NULL)
		return ENOMEM;

	strcpy(path, dir_path);
	strcat(path, "/");
	strcat(path, name);
	free(name);

	/* Check if the file exists */
	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		err = scanner_scan_file(this, path);
		close(fd);
		goto err0;
	}
	err = errno;
	if (err == ENOENT)	/* file not found. Try <...> */
		err = scanner_include_hseq(this, in_name);
err0:
	free(path);
	return err;
}

static
err_t scanner_scan_directive_include(struct scanner *this,
									 struct queue *line,
									 const char *dir_path)
{
	err_t err;
	bool closed;
	int name_len;
	const char *name;
	char *str;
	struct cpp_token *token;
	struct queue tokens;
	struct queue exp_line;
	enum lexer_token_type type;

	if (queue_is_empty(line))	/* no file-name */
		return EINVAL;

	queue_init(&exp_line, cpp_token_delete);
	queue_init(&tokens, cpp_token_delete);

	token = queue_peek_head(line);
	type = cpp_token_type(token);
	if (type == LXR_TOKEN_CHAR_STRING_LITERAL) {
		name = cpp_token_source(token);
		/* TODO: If the string has embedded nul chars? */
		err = scanner_include_qseq(this, dir_path, name);
		queue_delete_head(line);
		return err;
	}

	if (type != LXR_TOKEN_LESS_THAN) {
		/* repurpose expand_argument function */
		err =  scanner_expand_argument(this, line, &exp_line);
		queue_empty(line);
		if (err)
			return err;
		/* If the exp_line doesn't begin with a " or a < token, err */
		if (queue_is_empty(&exp_line))
			return EINVAL;
		token = queue_peek_head(&exp_line);
		type = cpp_token_type(token);
		if (type != LXR_TOKEN_CHAR_STRING_LITERAL &&
			type != LXR_TOKEN_LESS_THAN)
			return EINVAL;
		err = scanner_scan_directive_include(this, &exp_line, dir_path);
		queue_empty(&exp_line);
		return err;
	}

	assert(type == LXR_TOKEN_LESS_THAN);
	queue_delete_head(line);

	/* Form the path from tokens. maintain ws */
	closed = false;
	name_len = 0;
	while (!queue_is_empty(line)) {
		token = queue_remove_head(line);
		if (cpp_token_type(token) == LXR_TOKEN_GREATER_THAN) {
			closed = true;
			cpp_token_delete(token);
			break;
		}
		name_len += cpp_token_has_white_space(token) ? 1 : 0;
		name_len += cpp_token_source_length(token);
		err = queue_add_tail(&tokens, token);
		if (err)
			return err;
	}
	if (closed == false)
		return EINVAL;	/* closing > not found */

	name = str = malloc(name_len + 1);
	if (name == NULL)
		return ENOMEM;

	str[0] = 0;
	while (!queue_is_empty(&tokens)) {
		token = queue_remove_head(&tokens);
		assert(cpp_token_type(token) != LXR_TOKEN_GREATER_THAN);
		if (cpp_token_has_white_space(token))
			strcat(str, " ");
		strcat(str, cpp_token_source(token));
		cpp_token_delete(token);
	}
	err = scanner_include_hseq(this, name);
	free(str);
	return err;
}
/*****************************************************************************/
/*
 * returns ENOENT if no name matched the identifier.
 * returns ESUCCESS if full macro matched (i.e. a redefinition)
 * returns EINVAL if conflict.
 */
static
err_t scanner_check_macro_redefine(const struct scanner *this,
								   const struct macro *macro)
{
	const struct macro *m[2];
	const char *name[2];
	enum lexer_token_type type;

	if (this->is_running_predefined_macros)
		return ENOENT;	/* not found at all */

	m[0] = macro;
	type = cpp_token_type(macro->identifier);

	if (type == LXR_TOKEN_DEFINED)
		return EINVAL;
	if (type >= LXR_TOKEN_DATE && type <= LXR_TOKEN_HAS_INCLUDE &&
		type != LXR_TOKEN_STDC_NO_ATOMICS &&
		type != LXR_TOKEN_STDC_NO_COMPLEX &&
		type != LXR_TOKEN_STDC_NO_THREADS &&
		type != LXR_TOKEN_STDC_NO_VLA)
		return EINVAL;

	/* Check the list of macros to see if this is a redefine. */
	name[0] = cpp_token_resolved(macro->identifier);
	m[1] = scanner_find_macro(this, name[0]);
	if (m[1] == NULL)
		return ENOENT;	/* Not found at all */
	if (macro_are_identical(m))
		return ESUCCESS;
	/* Same identifier, but not identical. Raise error */
	return EINVAL;
}

static
err_t scanner_scan_directive_undef(struct scanner *this,
								   struct queue *line)
{
	int i;
	const char *name;
	struct cpp_token *ident;

	/* #undef without an ident is an error */
	if (queue_is_empty(line))
		return EINVAL;

	ident = queue_remove_head(line);
	if (!cpp_token_is_identifier(ident))
		return EINVAL;

	name = cpp_token_resolved(ident);
	i = scanner_find_macro_index(this, name);
	cpp_token_delete(ident);
	if (i < 0)
		return ESUCCESS;	/* Not defined */
	array_delete_entry(&this->macros, i);
	return ESUCCESS;
}

static
err_t scanner_scan_directive_define(struct scanner *this,
									struct queue *line)
{
	err_t err;
	struct macro *macro;
	struct cpp_token *ident, *token;

	/* #define without an ident is an error */
	if (queue_is_empty(line))
		return EINVAL;

	ident = queue_remove_head(line);
	if (!cpp_token_is_identifier(ident))
		return EINVAL;

	macro = malloc(sizeof(*macro));
	if (macro == NULL)
		return ENOMEM;

	/* No need to delete ident right-now */
	macro->identifier = ident;
	macro->is_function_like = false;
	macro->is_variadic = false;
	queue_init(&macro->parameters, cpp_token_delete);
	queue_init(&macro->replacement_list, cpp_token_delete);

	/* Check if this is a func-like or an obj-like macro */
	if (!queue_is_empty(line)) {
		token = queue_peek_head(line);
		if (cpp_token_type(token) == LXR_TOKEN_LEFT_PAREN &&
			cpp_token_is_first(token) == false &&
			cpp_token_has_white_space(token) == false) {
			/* free the left-paren */
			queue_delete_head(line);
			macro->is_function_like = true;
		}
	}

	err = ESUCCESS;
	if (macro->is_function_like)
		err = macro_scan_parameters(macro, line);
	if (!err)
		err = macro_scan_replacement_list(macro, line);
	if (err)
		return err;

	err = scanner_check_macro_redefine(this, macro);
	if (!err || err != ENOENT) {
		/*
		 * Equal to another macro, or incompatible with another macro of same
		 * name. Free in both cases */
		macro_delete(macro);
		return err;
	}
	assert(err == ENOENT);
	return array_add_entry(&this->macros, macro);
}
/*****************************************************************************/
static
bool rpn_stack_entry_sign(const struct rpn_stack_entry *this)
{
	assert(this->type != RPN_STACK_ENTRY_OPERATOR);
	if (this->type == RPN_STACK_ENTRY_UNSIGNED)
		return false;	/* +ve */
	if ((intmax_t)this->u.value < 0)
		return true;
	return false;
}

/*
 * -9223372036854775808 is 0x8000000000000000
 * 1.c:1:6: warning: integer constant is so large that it is unsigned
 *  1 | #if -9223372036854775808 << 1
 *    |      ^~~~~~~~~~~~~~~~~~~
 */

static
err_t rpn_stack_evaluate(struct queue *this,
						 struct rpn_stack_entry *out)
{
	err_t err;
	bool empty;
	int num_question_marks;
	bool sign[2];	/* false == +ve, true == -ve */
	uintmax_t value[2];
	struct queue result, stk;
	struct rpn_stack_entry *entry;
	struct rpn_stack_entry *operand[2];
	enum lexer_token_type operator;

	/*
	 * Reverse the stack so that we can use stack routines.
	 * a > b is represented as: 'a b > top'.
	 * But the processing of rpn starts at the bottom of this representation.
	 * So, reverse the stack.
	 */
	stack_init(&stk);
	queue_for_each_with_rem_rev(this, entry) {
		err = stack_push(&stk, entry);
		if (err)
			return err;
	}
	assert(queue_is_empty(this));
	*this = stk;
	stack_init(&stk);

	/* Default is false/0 */
	out->type = RPN_STACK_ENTRY_UNSIGNED;
	out->u.value = 0;

	stack_init(&result);
	while (!stack_is_empty(this)) {
		entry = stack_pop(this);

		/* Push non-operatror values on the stack. */
		if (entry->type == RPN_STACK_ENTRY_SIGNED ||
			entry->type == RPN_STACK_ENTRY_UNSIGNED) {
			err = stack_push(&result, entry);
			if (err)
				return err;
			continue;
		}

		operator = entry->u.operator;
		free(entry);	/* operator entry no longer needed */

		assert(!stack_is_empty(&result));
		operand[1] = stack_pop(&result);
		value[1] = operand[1]->u.value;
		sign[1] = rpn_stack_entry_sign(operand[1]);

		/* unary op? */
		switch (operator) {
		case LXR_TOKEN_UNARY_MINUS:
			value[1] = -(intmax_t)value[1];
			operand[1]->type = RPN_STACK_ENTRY_SIGNED;
			break;
		case LXR_TOKEN_EXCLAMATION_MARK:
			value[1] = !value[1];
			operand[1]->type = RPN_STACK_ENTRY_UNSIGNED;
			break;
		case LXR_TOKEN_TILDE:
			value[1] = ~value[1];
			operand[1]->type = RPN_STACK_ENTRY_SIGNED;
			break;
		default:
			break;
		}

		switch (operator) {
		case LXR_TOKEN_UNARY_MINUS:
		case LXR_TOKEN_EXCLAMATION_MARK:
		case LXR_TOKEN_TILDE:
			operand[1]->u.value = value[1];
			err = stack_push(&result, operand[1]);
			if (err)
				return err;
			continue;
		default:
			break;
		}

		assert(!stack_is_empty(&result));
		operand[0] = stack_pop(&result);
		value[0] = operand[0]->u.value;
		sign[0] = rpn_stack_entry_sign(operand[0]);

		if (operator == LXR_TOKEN_QUESTION_MARK) {
			num_question_marks = 1;
			/*
			 * op[0] has the initial condition, op[1] has the value when op[0]
			 * is true. rest, until the corresponding colon has the value of
			 * op[1] is false.
			 */
			empty = false;
			if (value[0]) {
				/* intiial cond is true. result is op[1], and free the rest */
				free(operand[0]);
				operand[0] = operand[1];
				empty = true;	/* empty the false part of the conditional */
			} else {
				/* initial cond is false. free op[1], and eval the rest */
				free(operand[1]);
			}

			/* Create a sub-stack, until the corresponding : */
			while (!stack_is_empty(this)) {
				entry = stack_peek(this);
				if (entry->u.operator == LXR_TOKEN_COLON &&
					num_question_marks == 1)
					break;
				stack_pop(this);
				if (entry->u.operator == LXR_TOKEN_QUESTION_MARK)
					++num_question_marks;
				else if (entry->u.operator == LXR_TOKEN_COLON)
					--num_question_marks;
				if (empty) {
					free(entry);
					continue;
				}
				err = stack_push(&stk, entry);
				if (err)
					return err;
			}
			if (stack_is_empty(this))
				return EINVAL;
			free(stack_pop(this));	/* free the corresponding colon */

			if (!empty)	/* evaluate the false part of the ?: conditional */
				err = rpn_stack_evaluate(&stk, operand[0]);
			if (!err)
				err = stack_push(&result, operand[0]);
			if (err)
				return err;
			assert(stack_is_empty(&stk));
			continue;
		}

		free(operand[1]);	/* no longer needed */
		switch (operator) {
		case LXR_TOKEN_LOGICAL_OR:
			value[0] = value[0] || value[1];
			operand[0]->type = RPN_STACK_ENTRY_UNSIGNED;
			break;
		case LXR_TOKEN_LOGICAL_AND:
			value[0] = value[0] && value[1];
			operand[0]->type = RPN_STACK_ENTRY_UNSIGNED;
			break;
		case LXR_TOKEN_BITWISE_OR:
			value[0] = value[0] | value[1];
			break;
		case LXR_TOKEN_BITWISE_XOR:
			value[0] = value[0] ^ value[1];
			break;
		case LXR_TOKEN_BITWISE_AND:
			value[0] = value[0] & value[1];
			break;
		case LXR_TOKEN_EQUALS:
			value[0] = value[0] == value[1];
			operand[0]->type = RPN_STACK_ENTRY_UNSIGNED;
			break;
		case LXR_TOKEN_NOT_EQUALS:
			value[0] = value[0] != value[1];
			operand[0]->type = RPN_STACK_ENTRY_UNSIGNED;
			break;
		case LXR_TOKEN_LESS_THAN:
			/*
			 * if both are +ve: i.e. both are unsigned, or both are signed but
			 * +ve, then usual.
			 * if both are -ve: i.e. both are signed, and both have msb set,
			 * then usual.
			 * if one of them is -ve, then usual.
			 */
			if (sign[0] == sign[1])
				value[0] = value[0] < value[1];
			else if (sign[0])	/* -ve < +ve is true */
				value[0] = 1;
			else
				value[0] = 0;	/* +ve < -ve is false */
			operand[0]->type = RPN_STACK_ENTRY_UNSIGNED;
			break;
		case LXR_TOKEN_LESS_THAN_EQUALS:
			if (sign[0] == sign[1])
				value[0] = value[0] <= value[1];
			else if (sign[0])	/* -ve <= +ve is true */
				value[0] = 1;
			else
				value[0] = 0;	/* +ve <= -ve is false */
			operand[0]->type = RPN_STACK_ENTRY_UNSIGNED;
			break;
		case LXR_TOKEN_GREATER_THAN:
			if (sign[0] == sign[1])
				value[0] = value[0] > value[1];
			else if (sign[0])	/* -ve > +ve is false */
				value[0] = 0;
			else
				value[0] = 1;	/* +ve > -ve is true */
			operand[0]->type = RPN_STACK_ENTRY_UNSIGNED;
			break;
		case LXR_TOKEN_GREATER_THAN_EQUALS:
			if (sign[0] == sign[1])
				value[0] = value[0] >= value[1];
			else if (sign[0])	/* -ve >= +ve is false */
				value[0] = 0;
			else
				value[0] = 1;	/* +ve >= -ve is true */
			operand[0]->type = RPN_STACK_ENTRY_UNSIGNED;
			break;
		case LXR_TOKEN_SHIFT_LEFT:
			if (value[1] > 63)
				value[0] = 0;
			value[0] <<= value[1];	/* signed may conver to unsinged */
			break;
		case LXR_TOKEN_SHIFT_RIGHT:
			if (value[1] > 63) {
				value[0] = 0;
				if (sign[0])
					value[0] = (uintmax_t)-1;
				break;
			}
			if (sign[0])
				value[0] = (uintmax_t)((intmax_t)value[0] >> value[1]);
			else
				value[0] >>= value[1];
			break;
		case LXR_TOKEN_PLUS:
			value[0] += value[1];
			break;
		case LXR_TOKEN_MINUS:
			value[0] -= value[1];
			break;
		case LXR_TOKEN_MUL:
			value[0] *= value[1];
			break;
		case LXR_TOKEN_DIV:
			if (value[1] == 0)
				return EINVAL;
			value[0] /= value[1];
			break;
		case LXR_TOKEN_MOD:
			if (value[1] == 0)
				return EINVAL;
			value[0] %= value[1];
			break;
		default:
			assert(0);
			return EINVAL;
		}	/* switch */
		operand[0]->u.value = value[0];
		err = stack_push(&result, operand[0]);
		if (err)
			return err;
	}	/* for */

	assert(!stack_is_empty(&result));
	entry = stack_pop(&result);
	*out = *entry;
	free(entry);
	assert(stack_is_empty(&result));
	return err;
}

/* this == operator stack */
static
err_t rpn_stack_push_operator(struct queue *this,
							  struct queue *out,
							  const enum lexer_token_type operator)
{
	err_t err;
	int priorities[2];
	struct rpn_stack_entry *entry, *top_entry;

	/*
	 * If opstk is empty, or has left-paren on tos, push.
	 * else if the tos has lower-priority operator, push
	 * else if the tos has the same priority operator, pop.
	 * Note all our operators are left-assoc.
	 */
	entry = malloc(sizeof(*entry));
	if (entry == NULL)
		return ENOMEM;
	entry->type = RPN_STACK_ENTRY_OPERATOR;
	entry->u.operator = operator;

	/* If the stack is empty, it will push the new entry and return */
	while (!stack_is_empty(this)) {
		top_entry = stack_peek(this);
		assert(top_entry->type == RPN_STACK_ENTRY_OPERATOR);

		/* If we hit a left-paren, push on top of it */
		if (top_entry->u.operator == LXR_TOKEN_LEFT_PAREN)
			return stack_push(this, entry);

		priorities[0] = rpn_operator_precedence(top_entry->u.operator);
		priorities[1] = rpn_operator_precedence(operator);

		/* the new op has higher prio than onstk-entry */
		if (priorities[0] > priorities[1])
			return stack_push(this, entry);

		/*
		 * only colon has the prio 12. if operator == colon, the
		 * on-stk-operator is also colon. note that : is right-assoc.
		 */
		if (priorities[0] == priorities[1] && operator == LXR_TOKEN_COLON)
			return stack_push(this, entry);

		/*
		 * same prior non-colons, or new-op has lower prio than on-stk.
		 * pop the top_entry and push it onto the output stack, and then
		 * recheck.
		 */
		stack_pop(this);	/* same prio => left-asocc behaviour */
		err = stack_push(out, top_entry);
		if (err)
			return err;
		/* recheck */
	}
	return stack_push(this, entry);
}

static
err_t cpp_token_scan_rpn_integer(const struct cpp_token *this,
								 struct rpn_stack_entry *out)
{
	int i, len;
	const char *str;
	uintmax_t value;

	out->type = RPN_STACK_ENTRY_UNSIGNED;	/* starts off as unsigned */
	str = cpp_token_source(this);
	len = strlen(str);

	value = 0;
	/* TODO support hex/oct/bin numbers too */
	for (i = 0; i < len; ++i) {
		if (i == len - 1 && !is_dec_digit(str[i])) {
			if (str[i] == 'L' || str[i] == 'l')
				continue;
			assert(0);
		}
		assert(is_dec_digit(str[i]));
		value *= 10;
		value += dec_digit_value(str[i]);
	}
	out->u.value = value;
	return ESUCCESS;
}

/*
 * We have cpp_token_type to represent operations, and
 * intmax_t, uintmax_t to represent signed/unsigned numbers.
 * all our binary operators are left-associative. i.e. when an operator is
 * being pushed on to the opstk and there already is another/same operator of
 * the same precedence on the opstk, that operator is popped off of the stack
 * and pushed on to the output.
 *
 * The precedence is given in the c2x standard by the way of arranging higher
 * precedence ops at a deeper depth in the grammar.
 *
 * state-machine.
 * state 0 == expecting_operand. for now we support unary-expr. We
 * also have to support char-constant and their conversion to exec-char-set.
 * state 1 == expecting_operator. binary operator.
 */
/* this is the tokens, out is the stack. out init by the caller */
static
err_t cpp_tokens_to_rpn(struct queue *this,
						struct queue *out)
{
	err_t err;
	int state, i, num_left_parens, num_question_marks;
	enum lexer_token_type type, operator;
	struct cpp_token *token;
	struct queue op_stk;
	struct rpn_stack_entry *entry;

#if 1
	queue_for_each(this, i, token) {
		if (cpp_token_has_white_space(token))
			printf(" ");
		printf("%s", cpp_token_source(token));
	}
	printf("\n");
#endif

	stack_init(&op_stk);
	state = num_left_parens = num_question_marks = 0;
	while (!queue_is_empty(this)) {
		token = queue_remove_head(this);
		type = operator = cpp_token_type(token);

		/* if expecting operand, then scan a unary-expression. */
		if (state == 0) {
			/* left-paren is allowed only in state 0 */
			if (type == LXR_TOKEN_LEFT_PAREN) {
				cpp_token_delete(token);
				entry = malloc(sizeof(*entry));
				if (entry == NULL)
					return ENOMEM;
				entry->type = RPN_STACK_ENTRY_OPERATOR;
				entry->u.operator = operator;
				err = stack_push(&op_stk, entry);
				if (err)
					return err;
				++num_left_parens;
				continue;
			}

			/* delete the token for non-numbers */
			if (type != LXR_TOKEN_NUMBER)
				cpp_token_delete(token);

			/* If this is a + unary op, ignore */
			if (type == LXR_TOKEN_PLUS)
				continue;

			/* Unary operators */
			if (operator == LXR_TOKEN_MINUS ||
				operator == LXR_TOKEN_EXCLAMATION_MARK ||
				operator == LXR_TOKEN_TILDE) {
				if (operator == LXR_TOKEN_MINUS)
					operator = LXR_TOKEN_UNARY_MINUS;
				err = rpn_stack_push_operator(&op_stk, out, operator);
				if (err)
					return err;
				/* stay in the same state for more unary-op, etc. */
				continue;
			}

			/*
			 * Scan an operand, and push on to the output stack.
			 * Change the state to expecting a binary operand.
			 */
			if (type != LXR_TOKEN_NUMBER)
				return EINVAL;

			entry = malloc(sizeof(*entry));
			if (entry == NULL)
				return ENOMEM;
			err = cpp_token_scan_rpn_integer(token, entry);
			cpp_token_delete(token);
			if (!err)
				err = stack_push(out, entry);
			if (err)
				return err;
			state = 1;	/* change state to expect a binary op */
			continue;
		}	/* state == 0 */

		assert(state == 1);
		cpp_token_delete(token);

		/* We expect right-paren after reading a number. i.e. in state = 1*/
		if (type == LXR_TOKEN_RIGHT_PAREN) {
			assert(num_left_parens);
			/*
			 * as long as we do not see a left-paren, pop entries off of the
			 * op_stk and push them onto the out stack
			 */
			while (!stack_is_empty(&op_stk)) {
				entry = stack_peek(&op_stk);
				if (entry->type == RPN_STACK_ENTRY_OPERATOR &&
					entry->u.operator == LXR_TOKEN_LEFT_PAREN)
					break;
				stack_pop(&op_stk);
				err = stack_push(out, entry);
				if (err)
					return err;
			}
			if (stack_is_empty(&op_stk))
				return EINVAL;
			stack_pop(&op_stk);	/* pop the left-paren */
			free(entry);
			/* after reading a right-paren, we stay in the same state. */
			--num_left_parens;
			continue;
		}

		/* first 3 are unary ops */
		for (i = 3; i < (int)ARRAY_SIZE(g_rpn_operator_precedence); ++i) {
			if (operator == g_rpn_operator_precedence[i].operator)
				break;
		}
		/* invalid binary op */
		if (i == (int)ARRAY_SIZE(g_rpn_operator_precedence))
			return EINVAL;
		/* The colon for question-mark is expected when in state 1 */
		if (operator == LXR_TOKEN_QUESTION_MARK) {
			++num_question_marks;
		} else if (operator == LXR_TOKEN_COLON) {
			assert(num_question_marks);
			--num_question_marks;
		}
		err = rpn_stack_push_operator(&op_stk, out, operator);
		if (err)
			return err;
		state = 0;	/* after binary op, go back to wanting operands. */
	}
	if (err)
		return err;

	/* If there's entries in the op_stk, push them onto the out */
	while (!stack_is_empty(&op_stk)) {
		entry = stack_pop(&op_stk);
		err = stack_push(out, entry);
		if (err)
			return err;
	}
	return err;
}

/* this is the tokens */
static
err_t cpp_tokens_evaluate_expression(struct queue *this,
									 int *out)
{
	err_t err;
	struct queue stk;
	struct rpn_stack_entry result;

	stack_init(&stk);
	err = cpp_tokens_to_rpn(this, &stk);
	if (err)
		return err;
	assert(queue_is_empty(this));
	assert(!stack_is_empty(&stk));
	err = rpn_stack_evaluate(&stk, &result);
	if (err)
		return err;
	assert(stack_is_empty(&stk));
	assert(result.type != RPN_STACK_ENTRY_OPERATOR);
	*out = result.u.value ? 1 : 0;
	return err;
}

/* Is any entry in wait/done state? */
static
bool cond_incl_stack_in_skip_zone(const struct queue *this)
{
	int i;
	const struct cond_incl_stack_entry *entry;

	/* The cond-incl stack is empty; do not skip. */
	if (stack_is_empty(this))
		return false;

	for (i = 0; i < stack_num_entries(this); ++i) {
		entry = stack_peek(this);
		if (entry->state == COND_INCL_STATE_WAIT ||
			entry->state == COND_INCL_STATE_DONE)
			return true;
	}
	return false;
}

/* line has tokens after #if */
static
err_t scanner_scan_directive_if(struct scanner *this,
								struct queue *line)
{
	err_t err;
	int num;
	bool has_white_space, is_first;
	enum lexer_token_type type;
	bool has_left_paren;
	const char *name, *str;
	struct cpp_token *token, *ident;
	struct queue tokens, exp_line;
	struct cond_incl_stack_entry *entry;
	struct queue *cistk;
	char32_t cp;

	/* There should be tokens */
	if (queue_is_empty(line))
		return EINVAL;

	queue_init(&tokens, cpp_token_delete);
	queue_init(&exp_line, cpp_token_delete);

	cistk = &this->cistk;
	entry = malloc(sizeof(*entry));
	if (entry == NULL)
		return ENOMEM;
	entry->type = LXR_TOKEN_DIRECTIVE_IF;

	/* If we are already inside a skip zone, just place done */
	if (cond_incl_stack_in_skip_zone(cistk)) {
		entry->state = COND_INCL_STATE_DONE;
		return stack_push(cistk, entry);
	}

	/* Scan defined ident, defined(ident) first. */
	while (!queue_is_empty(line)) {
		token = queue_remove_head(line);
		if (cpp_token_type(token) != LXR_TOKEN_DEFINED) {
			err = queue_add_tail(&tokens, token);
			if (err)
				return err;
			continue;
		}

		/* Found a defined. Save its props. */
		has_white_space = cpp_token_has_white_space(token);
		is_first = cpp_token_is_first(token);
		cpp_token_delete(token);
		if (queue_is_empty(line))
			return EINVAL;

		has_left_paren = false;
		token = queue_remove_head(line);
		if (cpp_token_type(token) == LXR_TOKEN_LEFT_PAREN) {
			if (queue_is_empty(line))
				return EINVAL;
			cpp_token_delete(token);
			has_left_paren = true;
			token = queue_remove_head(line);
		}
		if (!cpp_token_is_identifier(token))
			return EINVAL;
		ident = token;
		if (has_left_paren) {
			if (queue_is_empty(line))
				return EINVAL;
			token = queue_remove_head(line);
			if (cpp_token_type(token) != LXR_TOKEN_RIGHT_PAREN)
				return EINVAL;
			cpp_token_delete(token);
		}
		name = cpp_token_resolved(ident);
		num = scanner_find_macro(this, name) ? 1 : 0;
		cpp_token_delete(ident);
		err = cpp_token_new_number(num, has_white_space, is_first, &token);
		if (!err)
			err = queue_add_tail(&tokens, token);
		if (err)
			return err;
	}
	assert(queue_is_empty(line));
	assert(!queue_is_empty(&tokens));

	/* Now expand. repurpose expand_argument function */
	err = scanner_expand_argument(this, &tokens, &exp_line);
	queue_empty(&tokens);
	if (err)
		return err;

	/* true is replaced with 1, all other identifiers are replaced with 0 */
	assert(!queue_is_empty(&exp_line));
	while (!queue_is_empty(&exp_line)) {
		token = queue_remove_head(&exp_line);
		type = cpp_token_type(token);

		/* Can't have strings. */
		if (cpp_token_is_string_literal(token))
			return EINVAL;

		/* TODO: if the number is non-zero, replace it with 1 */
		if (type == LXR_TOKEN_NUMBER) {
			str = cpp_token_source(token);
			/* If the number has . in it, fail. floating point not allowed */
			if (strchr(str, '.'))
				return EINVAL;
		}

		/*
		 * if neither identifer, nor char-const, continue.
		 * numbers are also added here
		 */
		if (!cpp_token_is_identifier(token) &&
			!cpp_token_is_char_const(token)) {
			err = queue_add_tail(&tokens, token);
			if (err)
				return err;
			continue;
		}

		/* defined here is an undefined behaviour */
		if (type == LXR_TOKEN_DEFINED)
			return EINVAL;

		/* If this is a char-const, evaluate */
		if (cpp_token_is_char_const(token)) {
			err = lexer_token_evaluate_char_const(token->base, &cp);
			if (err)
				return err;
			num = cp;
		} else if (type == LXR_TOKEN_TRUE) {
			num = 1;
		} else {
			num = 0;
		}

		has_white_space = cpp_token_has_white_space(token);
		is_first = cpp_token_is_first(token);
		cpp_token_delete(token);

		/*
		 * For now, we only support +ve nums.
		 * If any header turns up with #if constructs of a number other than
		 * those, fix.
		 */
		assert(num >= 0);
		err = cpp_token_new_number(num, has_white_space, is_first, &token);
		if (!err)
			err = queue_add_tail(&tokens, token);
		if (err)
			return err;
	}
	assert(queue_is_empty(&exp_line));
	assert(!queue_is_empty(&tokens));

	err = cpp_tokens_evaluate_expression(&tokens, &num);
	if (err)
		return err;
	assert(queue_is_empty(&tokens));

	/* Default is to wait */
	entry->state = COND_INCL_STATE_WAIT;
	if (num)
		entry->state = COND_INCL_STATE_SCAN;
	return stack_push(cistk, entry);
}

/* line has tokens after #elif */
static
err_t scanner_scan_directive_elif(struct scanner *this,
								  struct queue *line)
{
	struct queue *cistk;
	struct cond_incl_stack_entry *entry;

	/* empty #elif is invalid */
	if (queue_is_empty(line))
		return EINVAL;

	cistk = &this->cistk;
	assert(!stack_is_empty(cistk));

	/* We must pop as the later in_skip_zone calc works with a stale value. */
	/* If the tos has else, invalid */
	entry = stack_pop(cistk);
	if (entry->type == LXR_TOKEN_DIRECTIVE_ELSE)
		return EINVAL;
	assert(entry->type == LXR_TOKEN_DIRECTIVE_IF);

	/* If the if was in scan/done state, or we are in skip-zone, done */
	if (entry->state == COND_INCL_STATE_SCAN ||
		entry->state == COND_INCL_STATE_DONE ||
		cond_incl_stack_in_skip_zone(cistk)) {
		entry->state = COND_INCL_STATE_DONE;
		return stack_push(cistk, entry);
	}
	free(entry);
	/* We aren't in a skip-zone. Place appropriate state. */
	return scanner_scan_directive_if(this, line);
}

static inline
err_t scanner_scan_directive_end_if(struct scanner *this)
{
	struct queue *cistk;
	struct cond_incl_stack_entry *entry;

	cistk = &this->cistk;
	assert(!stack_is_empty(cistk));
	entry = stack_pop(cistk);
	free(entry);
	return ESUCCESS;
}

static
err_t scanner_scan_directive_else(struct scanner *this)
{
	struct queue *cistk;
	struct cond_incl_stack_entry *entry;

	cistk = &this->cistk;
	assert(!stack_is_empty(cistk));

	/* We must pop as the later in_skip_zone calc works with a stale value. */
	/* If the tos already has else, invalid */
	entry = stack_pop(cistk);
	if (entry->type == LXR_TOKEN_DIRECTIVE_ELSE)
		return EINVAL;
	assert(entry->type == LXR_TOKEN_DIRECTIVE_IF);

	entry->type = LXR_TOKEN_DIRECTIVE_ELSE;

	/* If the if was in scan/done state, or we are in skip-zone, done */
	if (entry->state == COND_INCL_STATE_SCAN ||
		entry->state == COND_INCL_STATE_DONE ||
		cond_incl_stack_in_skip_zone(cistk)) {
		entry->state = COND_INCL_STATE_DONE;
		return stack_push(cistk, entry);
	}

	/* We aren't in a skip-zone, and if was in wait. place scan. */
	assert(entry->state == COND_INCL_STATE_WAIT);
	entry->state = COND_INCL_STATE_SCAN;
	return stack_push(cistk, entry);
}

/* the first token in the line is the identifier */
static
err_t scanner_scan_directive_elifdef(struct scanner *this,
									 const bool is_ndef,
									 struct queue *line)
{
	struct queue *cistk;
	struct cond_incl_stack_entry *entry;
	const char *name;
	struct cpp_token *token;
	const struct macro *macro;

	/* #ifndef without identifier is invalid. */
	if (queue_is_empty(line))
		return EINVAL;
	token = queue_remove_head(line);
	if (!cpp_token_is_identifier(token))
		return EINVAL;

	cistk = &this->cistk;
	assert(!stack_is_empty(cistk));

	/* We must pop as the later in_skip_zone calc works with a stale value. */
	/* If the tos has else, invalid */
	entry = stack_pop(cistk);
	if (entry->type == LXR_TOKEN_DIRECTIVE_ELSE)
		return EINVAL;
	assert(entry->type == LXR_TOKEN_DIRECTIVE_IF);

	/* If the if was in scan/done state, or we are in skip-zone, done */
	if (entry->state == COND_INCL_STATE_SCAN ||
		entry->state == COND_INCL_STATE_DONE ||
		cond_incl_stack_in_skip_zone(cistk)) {
		entry->state = COND_INCL_STATE_DONE;
		return stack_push(cistk, entry);
	}

	/* We aren't in a skip-zone. Place appropriate state. */
	/* Default is to wait */
	entry->state = COND_INCL_STATE_WAIT;
	name = cpp_token_resolved(token);
	macro = scanner_find_macro(this, name);
	if ((is_ndef && macro == NULL) || (!is_ndef && macro))
		entry->state = COND_INCL_STATE_SCAN;	/* true. */
	return stack_push(cistk, entry);
}

/* the first token in the line is the identifier */
static
err_t scanner_scan_directive_ifdef(struct scanner *this,
								   const bool is_ndef,
								   struct queue *line)
{
	struct cond_incl_stack_entry *entry;
	struct queue *cistk;
	const char *name;
	struct cpp_token *token;
	const struct macro *macro;

	/* #if[n]def without identifier is invalid. */
	if (queue_is_empty(line))
		return EINVAL;
	token = queue_remove_head(line);
	if (!cpp_token_is_identifier(token))
		return EINVAL;

	/* Allocate new entry, and push */
	cistk = &this->cistk;
	entry = malloc(sizeof(*entry));
	if (entry == NULL)
		return ENOMEM;
	entry->type = LXR_TOKEN_DIRECTIVE_IF;

	/* If we are already inside a skip zone, just place done */
	if (cond_incl_stack_in_skip_zone(cistk)) {
		cpp_token_delete(token);
		entry->state = COND_INCL_STATE_DONE;
		return stack_push(cistk, entry);
	}
	/* We aren't in a skip-zone. Place appropriate state. */

	/* Default is to wait */
	entry->state = COND_INCL_STATE_WAIT;
	name = cpp_token_resolved(token);
	macro = scanner_find_macro(this, name);
	cpp_token_delete(token);
	if ((is_ndef && macro == NULL) || (!is_ndef && macro))
		entry->state = COND_INCL_STATE_SCAN;	/* true. */
	return stack_push(cistk, entry);
}
/*****************************************************************************/
/* line starts after # */
static
err_t scanner_scan_directive(struct scanner *this,
							 struct queue *line,
							 const char *lexer_dir_path)	/* for includes */
{
	enum lexer_token_type type;
	struct queue *cistk;
	struct cpp_token *token;

	if (queue_is_empty(line))	/* # new-line. no effect. */
		return ESUCCESS;

	cistk = &this->cistk;
	token = queue_remove_head(line);
	type = cpp_token_type(token);
	cpp_token_delete(token);

	/* if/else when scanning directives are actually #if and #else */
	if (type == LXR_TOKEN_IF)
		type = LXR_TOKEN_DIRECTIVE_IF;
	if (type == LXR_TOKEN_ELSE)
		type = LXR_TOKEN_DIRECTIVE_ELSE;

	/* These constructs are peers to #if constructs. Handle them first. */
	switch (type) {
	case LXR_TOKEN_DIRECTIVE_ELSE_IF_DEFINED:
	case LXR_TOKEN_DIRECTIVE_ELSE_IF_NOT_DEFINED:
	case LXR_TOKEN_DIRECTIVE_ELSE_IF:
	case LXR_TOKEN_DIRECTIVE_ELSE:
	case LXR_TOKEN_DIRECTIVE_END_IF:
		/* The cistk shouldn't be empty */
		if (stack_is_empty(cistk))
			return EINVAL;
		break;
	default:
		break;
	}

	if (type == LXR_TOKEN_DIRECTIVE_ELSE_IF_DEFINED)
		return scanner_scan_directive_elifdef(this, false, line);
	if (type == LXR_TOKEN_DIRECTIVE_ELSE_IF_NOT_DEFINED)
		return scanner_scan_directive_elifdef(this, true, line);
	if (type == LXR_TOKEN_DIRECTIVE_ELSE_IF)
		return scanner_scan_directive_elif(this, line);
	if (type == LXR_TOKEN_DIRECTIVE_ELSE)
		return scanner_scan_directive_else(this);
	if (type == LXR_TOKEN_DIRECTIVE_END_IF)
		return scanner_scan_directive_end_if(this);

	/*
	 * type is not peer to #if's, is it another #if?
	 * #if, #ifdef and #ifndef can start new regions, even within other
	 * regions. We must track these if's so that their elif/else/endif can be
	 * tracked.
	 */
	if (type == LXR_TOKEN_DIRECTIVE_IF_DEFINED)
		return scanner_scan_directive_ifdef(this, false, line);
	if (type == LXR_TOKEN_DIRECTIVE_IF_NOT_DEFINED)
		return scanner_scan_directive_ifdef(this, true, line);
	if (type == LXR_TOKEN_DIRECTIVE_IF)
		return scanner_scan_directive_if(this, line);

	/* type isn't one of if,elif,else,endif. should skip these? */
	if (cond_incl_stack_in_skip_zone(cistk))
		return ESUCCESS;
	if (type == LXR_TOKEN_DIRECTIVE_DEFINE)
		return scanner_scan_directive_define(this, line);
	if (type == LXR_TOKEN_DIRECTIVE_INCLUDE)
		return scanner_scan_directive_include(this, line, lexer_dir_path);
	if (type == LXR_TOKEN_DIRECTIVE_UNDEF)
		return scanner_scan_directive_undef(this, line);
	return ENOTSUP;
}
/*****************************************************************************/
/* this == copy of repl. out init by caller */
static
err_t cpp_tokens_paste_object_like(struct queue *this,
								   struct queue *out)
{
	err_t err;
	bool is_non_str_double_hash;
	struct cpp_token_stream stream;
	char *str;
	struct lexer *lexer;
	int len;
	struct cpp_token *next, *prev, *curr;

	err = ESUCCESS;
	prev = NULL;

	/* this should not be empty */
	assert(!queue_is_empty(this));

	while (!queue_is_empty(this)) {
		curr = queue_remove_head(this);
		if (cpp_token_type(curr) != LXR_TOKEN_DOUBLE_HASH) {
			if (prev) {
				err = queue_add_tail(out, prev);
				if (err)
					return err;
			}
			prev = curr;
			continue;
		}

		/* curr is ## */
		if (prev == NULL || queue_is_empty(this))
			return EINVAL;

		next = queue_remove_head(this);

		is_non_str_double_hash = false;
		if (cpp_token_type(prev) == LXR_TOKEN_HASH &&
			cpp_token_type(next) == LXR_TOKEN_HASH)
			is_non_str_double_hash = true;

		len = 0;
		len += cpp_token_source_length(prev);
		len += cpp_token_source_length(next);
		str = malloc(len + 1 + 1);
		if (str == NULL)
			return ENOMEM;
		strcpy(str, cpp_token_source(prev));
		strcat(str, cpp_token_source(next));
		strcat(str, "\n");

		cpp_token_delete(curr);
		cpp_token_delete(prev);
		cpp_token_delete(next);
		prev = NULL;

		err = lexer_new(NULL, str, len, &lexer);
		if (err)
			return err;
		cpp_token_stream_init(&stream, lexer);
		err = cpp_token_stream_remove_head(&stream, &curr);
		if (err)
			return err;
		err = cpp_token_stream_remove_head(&stream, &next);
		if (!err) {
			/* Successfully lexed another token. Fail */
			cpp_token_delete(curr);
			cpp_token_delete(next);
			err = EINVAL;
		}
		if (err != EOF)	/* Error isn't EEOF. Fail */
			return err;

		assert(cpp_token_stream_is_empty(&stream));
		lexer_delete(lexer);	/* deletes str also */
		if (is_non_str_double_hash) {
			assert(cpp_token_type(curr) == LXR_TOKEN_DOUBLE_HASH);
			curr->base->type = LXR_TOKEN_NON_STRINGIZING_DOUBLE_HASH;
		}
		err = queue_add_tail(out, curr);
		if (err)
			return err;
	}
	assert(err == ESUCCESS);
	assert(queue_is_empty(this));
	if (prev)
		return queue_add_tail(out, prev);
	return err;
}

/* out init by caller */
static
err_t cpp_tokens_expand_object_like(const struct queue *repl,
									struct queue *out)
{
	err_t err = ESUCCESS;
	struct queue res;

	/* empty repl-list is allowed */
	if (queue_is_empty(repl))
		return err;

	/* Work with a copy of the repl */
	queue_init(&res, cpp_token_delete);
	err = cpp_tokens_copy(repl, &res);
	if (err)
		return err;
	err = cpp_tokens_paste_object_like(&res, out);
	assert(queue_is_empty(&res));
	return err;
}
/*****************************************************************************/
/*
 * the has_white_space and is_first applies to the string, not to the element
 * that is at the beginning of the stringizing arg list.
 */
static
err_t cpp_tokens_stringize(const struct queue *this,
						   const bool has_white_space,
						   const bool is_first,
						   struct cpp_token **out)
{
	err_t err;
	int i, len, j, k, src_len;
	const char *src;
	char *str;
	struct cpp_token_stream stream;
	struct lexer *lexer;
	struct cpp_token *token;
	const struct cpp_token *t;

	/* Empty string */
	if (this == NULL || queue_is_empty(this)) {
		str = malloc(4);
		if (str == NULL)
			return ENOMEM;
		str[0] = str[1] = '\"';
		str[2] = '\n';
		str[3] = NULL_CHAR;
		len = 3;
		goto build;
	}
	assert(!queue_is_empty(this));

	len = 3;	/* for the dbl quotes + nl*/
	queue_for_each(this, i, t) {
		/* We may have to remove placemarkers */
		assert(cpp_token_type(t) != LXR_TOKEN_PLACE_MARKER);
		if (i)
			len += cpp_token_has_white_space(t);
		/* replace \ with \\, and " with \" */
		src = cpp_token_source(t);
		src_len = cpp_token_source_length(t);
		for (j = 0; j < src_len; ++j) {
			if (src[j] == '\"' || src[j] == '\\')
				++len;
			++len;
		}
	}
	str = malloc(len + 1);
	if (str == NULL)
		return ENOMEM;
	str[len - 1] = '\n';
	str[len] = NULL_CHAR;

	k = 0;
	str[k++] = '\"';
	queue_for_each(this, i, t) {
		if (i && cpp_token_has_white_space(t))
			str[k++] = ' ';
		/* replace \ with \\, and " with \" */
		src = cpp_token_source(t);
		src_len = cpp_token_source_length(t);
		for (j = 0; j < src_len; ++j) {
			if (src[j] == '\"' || src[j] == '\\')
				str[k++] = '\\';
			str[k++] = src[j];
		}
	}
	str[k++] = '\"';
	assert(k == len - 1);
build:
	err = lexer_new(NULL, str, len, &lexer);
	if (err)
		return err;
	cpp_token_stream_init(&stream, lexer);
	err = cpp_token_stream_remove_head(&stream, &token);
	if (err)
		return err;
	lexer_delete(lexer);
	assert(cpp_token_is_string_literal(token));
	assert(cpp_token_type(token) == LXR_TOKEN_CHAR_STRING_LITERAL);
	token->has_white_space = has_white_space;
	token->is_first = is_first;
	*out = token;
	return err;
}

/*
 * #va-opt: usual rules for its args. Any parameter not subject to # or ## is
 * replaced by corresponding exp_arg. This applies to va-args too.
 * Any parameter subject to # or ## is replaced by corresponding arg. This
 * applies to va-args too. Since this va-opt is subject to stringization, any
 * macro calls within va-opt pp-tokens are not expanded.
 */
/* Starts at the openeing left-paren. out init by caller. */
static
err_t cpp_tokens_collect_va_opt_args(struct queue *repl,
									 struct queue *out)
{
	err_t err;
	enum lexer_token_type type;
	int num_internal_left_parens;
	struct cpp_token *token;

	/* Should begin with a left-paren */
	if (queue_is_empty(repl))
		return EINVAL;
	token = queue_remove_head(repl);
	if (cpp_token_type(token) != LXR_TOKEN_LEFT_PAREN)
		return EINVAL;
	cpp_token_delete(token);

	/* Should at least have a right-paren */
	if (queue_is_empty(repl))
		return EINVAL;

	/* Is the expression empty? */
	token = queue_peek_head(repl);
	if (cpp_token_type(token) == LXR_TOKEN_RIGHT_PAREN) {
		queue_delete_head(repl);
		return ESUCCESS;
	}

	num_internal_left_parens = 0;
	while (!queue_is_empty(repl)) {
		token = queue_remove_head(repl);
		type = cpp_token_type(token);

		/* Can't have va-opt inside va-opt */
		if (type == LXR_TOKEN_VA_OPT)
			return EINVAL;

		/* This is the closing right-paren */
		if (type == LXR_TOKEN_RIGHT_PAREN && num_internal_left_parens == 0) {
			cpp_token_delete(token);
			return ESUCCESS;
		}
		err = queue_add_tail(out, token);
		if (err)
			return err;
		if (type == LXR_TOKEN_LEFT_PAREN)
			++num_internal_left_parens;
		else if (type == LXR_TOKEN_RIGHT_PAREN)
			--num_internal_left_parens;
	}
	/* repl is empty, and the closing right-paren was not found. */
	return EINVAL;
}


/* Consumes left, right */
static
err_t cpp_token_paste(struct cpp_token *left,
					  struct cpp_token *right,
					  struct cpp_token **out)
{
	int len;
	bool is_left_place_marker, is_right_place_marker;
	char *str;
	err_t err;
	struct cpp_token *curr, *next;
	struct lexer *lexer;
	struct cpp_token_stream stream;

	is_right_place_marker = is_left_place_marker = false;
	if (cpp_token_type(left) == LXR_TOKEN_PLACE_MARKER)
		is_left_place_marker = true;
	if (cpp_token_type(right) == LXR_TOKEN_PLACE_MARKER)
		is_right_place_marker = true;

	/* If left is a place-marker, return right */
	if (is_left_place_marker) {
		cpp_token_delete(left);
		*out = right;
		return ESUCCESS;
	}

	/* Else, if right is a place-marker, return left */
	if (is_right_place_marker) {
		cpp_token_delete(right);
		*out = left;
		return ESUCCESS;
	}

	/* none of them is a place-marker. */
	len = 1;	/* nl */
	len += cpp_token_source_length(left);
	len += cpp_token_source_length(right);
	str = malloc(len + 1);
	if (str == NULL)
		return ENOMEM;

	strcpy(str, cpp_token_source(left));
	strcat(str, cpp_token_source(right));
	strcat(str, "\n");
	cpp_token_delete(left);
	cpp_token_delete(right);

	err = lexer_new(NULL, str, len, &lexer);
	if (err)
		return err;

	cpp_token_stream_init(&stream, lexer);
	err = cpp_token_stream_remove_head(&stream, &curr);
	if (err)
		goto err0;
	err = cpp_token_stream_remove_head(&stream, &next);
	if (!err) {
		cpp_token_delete(curr);
		cpp_token_delete(next);
		err = EINVAL;
		goto err0;
	}
	if (err != EOF)
		goto err0;
	*out = curr;
	err = ESUCCESS;
	assert(cpp_token_stream_is_empty(&stream));
err0:
	lexer_delete(lexer);
	return err;
}
/*
 * Stringizing: all parameters are replaced by corresponding arg. This applies
 * to va-args too.
 * #va-opt: usual rules for its args. Any parameter not subject to # or ## is
 * replaced by corresponding exp_arg. This applies to va-args too.
 * Any parameter subject to # or ## is replaced by corresponding arg. This
 * applies to va-args too. Since this va-opt is subject to stringization, any
 * macro calls within va-opt pp-tokens are not expanded.
 */

/* ident + others */
static
err_t scanner_arg_substitution_others(struct scanner *this,
									  const struct macro *macro,
									  const struct queue *args,
									  const struct queue *exp_args,
									  const int num_args,
									  const bool is_pasting,
									  struct queue *repl,
									  struct queue *out)
{
	err_t err;
	bool has_white_space, is_first;
	const char *name;
	int index, num_params;
	enum lexer_token_type type;
	struct cpp_token *token;
	(void)this;

	assert(!queue_is_empty(repl));

	num_params = queue_num_entries(&macro->parameters);

	token = queue_remove_head(repl);
	type = cpp_token_type(token);

	/*
	 * If it is not an identifier, it can't be va-args, va-opt or a
	 * parameter. Simply add it.
	 */
	if (!cpp_token_is_identifier(token))
		return queue_add_tail(out, token);

	/* identifier, but not any of the va-arg, va-opts and param. */
	name = cpp_token_resolved(token);
	index = macro_find_parameter(macro, name);
	if (type != LXR_TOKEN_VA_ARGS && index == EOF)
		return queue_add_tail(out, token);

	/*
	 * If the arg is absent or is empty, and is_pasting, place-marker.
	 * We can get away with not inserting a p-m when arg is absent/empty and we
	 * are not pasting, but in order to maintain space positions, we can insert
	 * the p-m as a carrier of the npws info for the va-args expansion.
	 * TODO remove the 'if (!is_pasting)' cond below.
	 */
	if (type == LXR_TOKEN_VA_ARGS) {
		assert(macro->is_variadic);
		if (num_args == num_params - 1 ||
			queue_is_empty(&args[num_args - 1])) {
			if (!is_pasting) {
				cpp_token_delete(token);
				return ESUCCESS;
			}
			has_white_space = cpp_token_has_white_space(token);
			is_first = cpp_token_is_first(token);
			cpp_token_delete(token);
			err = cpp_token_new_place_marker(has_white_space, is_first,
											 &token);
			if (!err)
				err = queue_add_tail(out, token);
			return err;
		}
		/*
		 * TODO: when not pasting, pass the token's npws info to the first of
		 * the exp_args.
		 */
		cpp_token_delete(token);
		if (is_pasting)
			return cpp_tokens_copy(&args[num_args - 1], out);
		return cpp_tokens_copy(&exp_args[num_args - 1], out);
	}
	assert(index >= 0);
	if (is_pasting) {
		if (queue_is_empty(&args[index])) {
			has_white_space = cpp_token_has_white_space(token);
			is_first = cpp_token_is_first(token);
			cpp_token_delete(token);
			err = cpp_token_new_place_marker(has_white_space, is_first,
											 &token);
			if (!err)
				err = queue_add_tail(out, token);
			return err;
		}
		cpp_token_delete(token);
		return cpp_tokens_copy(&args[index], out);
	}
	/* TODO here too, the token should donate its npws info to the exp_args. */
	cpp_token_delete(token);
	return cpp_tokens_copy(&exp_args[index], out);
}

/*
 * If the repl contains more ## in chains, such chains are also processed here.
 * repl must not start with ##.
 */
static
err_t scanner_arg_substitution_paste(struct scanner *this,
									 const struct macro *macro,
									 const struct queue *args,
									 const struct queue *exp_args,
									 const int num_args,
									 struct queue *repl,
									 struct queue *out)
{
	err_t err;
	struct queue res;
	struct cpp_token *prev, *next, *token;

	/*
	 * It is known for sure at the start of the loop that the first token is a
	 * non-hash token, and the second is ##. Perform arg-subs for the first
	 * token.
	 */
	queue_init(&res, cpp_token_delete);
	err = scanner_arg_substitution_one(this, macro, args, exp_args,
									   num_args, true, repl, out);
	if (err)
		return err;

	/* Take the last token of out */
	assert(!queue_is_empty(out));
	prev = queue_remove_tail(out);
	while (!queue_is_empty(repl)) {
		token = queue_peek_head(repl);
		if (cpp_token_type(token) != LXR_TOKEN_DOUBLE_HASH)
			break;
		queue_delete_head(repl);

		/* Can't have ## end */
		if (queue_is_empty(repl))
			return EINVAL;

		/* Can't have ## ## */
		token = queue_peek_head(repl);
		if (cpp_token_type(token) == LXR_TOKEN_DOUBLE_HASH)
			return EINVAL;

		err = scanner_arg_substitution_one(this, macro, args, exp_args,
										   num_args, true, repl, &res);
		if (err)
			return err;
		assert(!queue_is_empty(&res));

		/* Take the first token in res */
		next = queue_remove_head(&res);
		err = cpp_token_paste(prev, next, &token);
		if (!err)
			err = queue_add_tail(out, token);
		if (err)
			return err;
		queue_move(&res, out);
		/* Take the last token of out */
		assert(!queue_is_empty(out));
		prev = queue_remove_tail(out);
	}
	assert(err == ESUCCESS);
	if (prev)
		err = queue_add_tail(out, prev);
	return err;
}

/* out init by caller */
static
err_t scanner_arg_substitution_one(struct scanner *this,
								   const struct macro *macro,
								   const struct queue *args,
								   const struct queue *exp_args,
								   const int num_args,
								   const bool is_pasting,
								   struct queue *repl,
								   struct queue *out)
{
	err_t err;
	bool has_white_space, is_first;
	int index, num_params;
	bool is_stringizing;
	enum lexer_token_type type;
	struct queue va_args, va_exp_args;
	struct cpp_token *token, *next;
	const char *name;

	queue_init(&va_args, cpp_token_delete);
	queue_init(&va_exp_args, cpp_token_delete);
	num_params = queue_num_entries(&macro->parameters);

	/* repl must not be empty */
	assert(!queue_is_empty(repl));

	token = queue_remove_head(repl);
	type = cpp_token_type(token);

	/* These are consumed internally. */
	if (type == LXR_TOKEN_DOUBLE_HASH)
		return EINVAL;

	assert(macro->is_function_like);
	is_stringizing = type == LXR_TOKEN_HASH;
	if (is_stringizing) {
		has_white_space = cpp_token_has_white_space(token);
		is_first = cpp_token_is_first(token);
		cpp_token_delete(token);

		/* We should have a parameter next. */
		if (queue_is_empty(repl))
			return EINVAL;

		token = queue_remove_head(repl);
		type = cpp_token_type(token);
		if (!cpp_token_is_identifier(token))
			return EINVAL;

		/* Should be one of va-opt, va-args, or param */
		name = cpp_token_resolved(token);
		index = macro_find_parameter(macro, name);
		if (type != LXR_TOKEN_VA_OPT &&
			type != LXR_TOKEN_VA_ARGS &&
			index == EOF)
			return EINVAL;

		/* free the ident */
		cpp_token_delete(token);
		if (index >= 0) {	/* this is the non-va param */
			assert(type != LXR_TOKEN_VA_OPT);
			assert(type != LXR_TOKEN_VA_ARGS);
			err = cpp_tokens_stringize(&args[index], has_white_space,
									   is_first, &token);
			goto hash_done;
		}

		/* If the arg is absent, or is empty, then "" */
		if (type == LXR_TOKEN_VA_ARGS) {
			assert(macro->is_variadic);
			if (num_args == num_params - 1)
				err = cpp_tokens_stringize(NULL, has_white_space, is_first,
										   &token);
			else
				err = cpp_tokens_stringize(&args[num_args - 1],
										   has_white_space, is_first,
										   &token);
			goto hash_done;
		}
		/* This token is not in the repl queue */
		assert(type == LXR_TOKEN_VA_OPT);
		goto va_opt;
hash_done:
		if (err)
			return err;
		goto check_paste;
	}
va_opt:
	if (type == LXR_TOKEN_VA_OPT) {
		assert(macro->is_variadic);
		if (!is_stringizing) {
			has_white_space = cpp_token_has_white_space(token);
			is_first = cpp_token_is_first(token);
			cpp_token_delete(token);
		}

		/*
		 * token points to va-opt token. unless you come from stringizing,
		 * where token has been freed already.
		 */
		/* Collect args. */
		err = cpp_tokens_collect_va_opt_args(repl, &va_args);
		if (err)
			return err;

		/*
		 * When stringizing, if the arg is absent, or its expansion is empty,
		 * then "". Expanson can be empty in two ways: by exp_args[num_args -1]
		 * being emtpy, and by va_args begin empty.
		 */
		if (num_args == num_params - 1 ||
			queue_is_empty(&va_args) ||
			queue_is_empty(&exp_args[num_args - 1])) {
			queue_empty(&va_args);
			if (is_stringizing) {
				err = cpp_tokens_stringize(NULL, has_white_space, is_first,
										   &token);
				goto hash_done;
			}
			err = cpp_token_new_place_marker(has_white_space, is_first,
											 &token);
			if (err)
				return err;
			goto check_paste;
		}

		/* substitute */
		err = scanner_arg_substitution(this, macro, args, exp_args, num_args,
									   &va_args, &va_exp_args);
		if (err)
			return err;
		assert(queue_is_empty(&va_args));

		/*
		 * TODO:
		 * Follow the e.g. of scanner_process_one that donates the npws from
		 * the ident to the first token of the exp-list.
		 */
		assert(!queue_is_empty(&va_exp_args));
#if 0
		if (!queue_is_empty(&va_exp_args)) {
			token = cpp_tokens_peek_head(&va_exp_args);
			token->base->num_prev_white_spaces =
				src->base->num_prev_white_spaces;
		}
#endif

		/* Do not remove place-markers yet. We have to check for pasting. */
		if (is_stringizing) {
			/*
			 * No problems removing the place-markers here. The tokens will be
			 * converted to a string, and won't interfere with any later token
			 * pasting.
			 */
			cpp_tokens_remove_place_markers(&va_exp_args);
			err = cpp_tokens_stringize(&va_exp_args, has_white_space, is_first,
									   &token);
			assert(queue_is_empty(&va_exp_args));
			goto hash_done;
		}

		/*
		 * To check for paste, we must set token to the last of the
		 * va_exp_args.
		 */
		assert(!queue_is_empty(&va_exp_args));
		queue_move(&va_exp_args, out);
		token = queue_remove_tail(out);
		/* fall thru */
	}
check_paste:
	/* Whatever token is, check for token ##, before adding the otken. */
	if (is_pasting == false && !queue_is_empty(repl)) {
		next = queue_peek_head(repl);
		type = cpp_token_type(next);
		if (type == LXR_TOKEN_DOUBLE_HASH) {
			err = queue_add_head(repl, token);
			if (!err)
				err = scanner_arg_substitution_paste(this, macro, args,
													 exp_args, num_args, repl,
													 out);
			return err;
		}
		/* else, token is not followed by ## */
	}
	err = queue_add_head(repl, token);
	if (!err)
		err = scanner_arg_substitution_others(this, macro, args, exp_args,
											  num_args, is_pasting, repl, out);
	return err;
}

static
err_t scanner_arg_substitution(struct scanner *this,
							   const struct macro *macro,
							   const struct queue *args,
							   const struct queue *exp_args,
							   const int num_args,
							   struct queue *repl,
							   struct queue *out)
{
	err_t err;

	while (!queue_is_empty(repl)) {
		err = scanner_arg_substitution_one(this, macro, args, exp_args,
										   num_args, false, repl, out);
		if (err)
			return err;
	}
	return err;
}

static
err_t scanner_expand_function_like(struct scanner *this,
								   const struct macro *macro,
								   const struct queue *args,
								   const struct queue *exp_args,
								   const int num_args,
								   struct queue *out)

{
	err_t err;
	struct queue repl;

	if (queue_is_empty(&macro->replacement_list))
		return ESUCCESS;

	queue_init(&repl, cpp_token_delete);
	err = cpp_tokens_copy(&macro->replacement_list, &repl);
	if (err)
		return err;
	/* Consumes the copy of the repl-list */
	err = scanner_arg_substitution(this, macro, args, exp_args, num_args,
								   &repl, out);
	assert(queue_is_empty(&repl));
	return err;
}

/* Caller inits out */
static
err_t scanner_expand_argument(struct scanner *this,
							  const struct queue *arg,
							  struct queue *out)
{
	err_t err;
	struct cpp_token *token;
	struct queue exp_arg;
	struct cpp_token_stream stream;
	struct queue mstk;
	struct cpp_token repl_list_end;
	struct lexer_token _repl_list_end;

	/* repl-list-end-marker */
	lexer_token_init(&_repl_list_end);
	repl_list_end.base = &_repl_list_end;;
	repl_list_end.base->type = LXR_TOKEN_REPL_LIST_END;

	queue_init(&exp_arg, cpp_token_delete);

	/* Argument expansion is done on a fresh stack */
	stack_init(&mstk);

	cpp_token_stream_init(&stream, NULL);
	err = cpp_tokens_copy(arg, &stream.tokens);
	if (err)
		return err;
	cpp_token_stream_add_tail(&stream, &repl_list_end);

	while (true) {
		err = cpp_token_stream_peek_head(&stream, &token);
		assert(err == ESUCCESS);

		/* End of the stream */
		if (token == &repl_list_end) {
			err = cpp_token_stream_remove_head(&stream, &token);
			assert(err == ESUCCESS);
			assert(cpp_token_stream_is_empty(&stream));
			break;
		}

		err = scanner_process_one(this, &mstk, &stream, &exp_arg);
		if (!err)
			err = queue_move(&exp_arg, out);
		if (!err)
			continue;

		/* Handle EPARTIAL here */
		if (err == EPARTIAL) {
			/* Our barrier was signalled. Return success to the caller. */
			assert(queue_is_empty(&exp_arg));
			assert(!cpp_token_stream_is_empty(&stream));
			err = queue_move(&stream.tokens, out);
			if (err)
				return err;
			break;
		}
		if (err)
			return err;
	}
	return ESUCCESS;
}

/*
 * When collecting the variadic arguments, we must also collect commas.
 * converts eof into epartial. out is stored into if the invocation is partial.
 */
static
err_t cpp_token_stream_collect_arguments(struct cpp_token_stream *stream,
										 const struct macro *macro,
										 struct queue **out_args,
										 int *out_num_args)
{
	err_t err;
	int num_args, num_internal_left_parens, num_params;
	enum lexer_token_type type;
	struct cpp_token *token, *ident, *left_paren;
	struct queue tokens;
	struct queue *args, *arg;

	err = cpp_token_stream_remove_head(stream, &ident);
	assert(err == ESUCCESS);
	err = cpp_token_stream_remove_head(stream, &left_paren);
	assert(err == ESUCCESS);

	num_params = queue_num_entries(&macro->parameters);
	num_args = 1;	/* empty () is also an arg */
	queue_init(&tokens, cpp_token_delete);

	/* First, check if we have a full-invocation or a partial-invocation */
	num_internal_left_parens = 0;
	while (true) {
		err = cpp_token_stream_remove_head(stream, &token);

		/* EOF implies the stream is partial. Any other error is fatal. */
		if (err == EOF)
			err = EPARTIAL;
		if (err)
			break;

		/*
		 * We hit the repl-end of the macro currently on the top of mstk,
		 * before we could complete scanning a full invocation. This repl-end
		 * doesn't belong to the macro passed here; it belongs to the
		 * parent-macro. The markers are allocated on stack; cannot free them.
		 * Return EPARTIAL.
		 */
		type = cpp_token_type(token);
		if (type == LXR_TOKEN_REPL_LIST_END) {
			err = EPARTIAL;
			break;
		}

		/*
		 * This is the arg-list ending right-paren.
		 * We have a full invocation.
		 */
		if (type == LXR_TOKEN_RIGHT_PAREN && num_internal_left_parens == 0) {
			cpp_token_delete(token);
			break;
		}
		err = queue_add_tail(&tokens, token);
		if (err)
			return err;

		/*
		 * This is an arg-separating comma. num_args increases.
		 * Unless we are scanning the variadic zone.
		 */
		if (type == LXR_TOKEN_COMMA && num_internal_left_parens == 0) {
			if (macro->is_variadic && num_args == num_params)
				continue;	/* won't incr num_args any more */
			++num_args;
			continue;
		}

		if (type == LXR_TOKEN_LEFT_PAREN)
			++num_internal_left_parens;
		else if (type == LXR_TOKEN_RIGHT_PAREN)
			--num_internal_left_parens;
	}

	/*
	 * partial invocation only. need to go up one level & retry.
	 * return EPARTIAL.
	 */
	if (err == EPARTIAL) {
		/*
		 * Do not place repl-list-end that was removed above. the retval of
		 * EPARTIAL is enough of a signal.
		 */

		/* Reconstruct the stream */
		assert(!queue_is_empty(&tokens));
		queue_for_each_with_rem_rev(&tokens, token) {
			err = cpp_token_stream_add_head(stream, token);
			if (err)
				return err;
		}
		if (!err)
			assert(queue_is_empty(&tokens));
		if (!err)
			err = cpp_token_stream_add_head(stream, left_paren);
		if (!err)
			err = cpp_token_stream_add_head(stream, ident);
		if (!err)
			err = EPARTIAL;	/* If stream rebuilt, return the orig err. */
		return err;
	}

	/* full-invocation present */
	cpp_token_delete(ident);
	cpp_token_delete(left_paren);

	/*
	 * For a macro that takes no params, change empty-arg to none so that
	 * num_args matches with num_params. For all other macros, an arg that is
	 * absent is sent as empty.
	 */
	if (num_params == 0 && queue_num_entries(&tokens) == 0)
		num_args = 0;

	/* if the macro is not variadic, then num_args must equal num_params */
	if (macro->is_variadic == false &&
		num_args != num_params)
		return EINVAL;

	/*
	 * if the macro is variadic, num_args must be num_params - 1, or
	 * num_params.
	 */
	if (macro->is_variadic &&
		num_args != num_params - 1 &&
		num_args != num_params)
		return EINVAL;

	if (num_args == 0) {
		*out_num_args = 0;
		*out_args = NULL;
		return ESUCCESS;
	}

	args = malloc(num_args * sizeof(*args));
	if (args == NULL)
		return ENOMEM;

	/*
	 * The empty token-seq is considered one arg. Later, after confirming that
	 * the invocation has no explicit args, num_args is set to 0.
	 */
	arg = &args[0];
	queue_init(arg, cpp_token_delete);
	num_args = 1;
	num_internal_left_parens = 0;
	while (!queue_is_empty(&tokens)) {
		token = queue_remove_head(&tokens);
		type = cpp_token_type(token);

		/*
		 * Any token that has is_first == true and has_white_space == false
		 * convert that to is_first == false and has_white_space == true.
		 * This straightens the arg-list.
		 */
		if (cpp_token_is_first(token) && !cpp_token_has_white_space(token)) {
			token->is_first = false;
			token->has_white_space = true;
		}

		/*
		 * This is an arg-separating comma. num_args increases.
		 * Unless we are scanning the variadic zone.
		 */
		if (type == LXR_TOKEN_COMMA && num_internal_left_parens == 0) {
			if (macro->is_variadic && num_args == num_params) {
				err = queue_add_tail(arg, token);
				if (err)
					return err;
				continue;
			}
			cpp_token_delete(token);
			++arg;
			++num_args;
			queue_init(arg, cpp_token_delete);
			continue;
		}
		if (type == LXR_TOKEN_LEFT_PAREN)
			++num_internal_left_parens;
		else if (type == LXR_TOKEN_RIGHT_PAREN)
			--num_internal_left_parens;
		err = queue_add_tail(arg, token);
		if (err)
			return err;
	}
	assert(queue_is_empty(&tokens));
	*out_num_args = num_args;
	*out_args = args;
	return ESUCCESS;
}

/*
 * As long as there are tokens to the left of the repl-end-marker, continue
 * processing the stream. out is init by caller.
 */
static
err_t scanner_process_one(struct scanner *this,
						  struct queue *mstk,
						  struct cpp_token_stream *stream,
						  struct queue *out)
{
	err_t err;
	int i, num_args;
	const char *name;
	struct cpp_token *token, *ident;
	struct queue *args, *exp_args;
	const struct queue *repl;
	const struct macro *macro, *m;
	struct cpp_token repl_list_end;
	struct lexer_token _repl_list_end;
	bool is_ident, is_marked, is_macro, is_active, has_white_space;
	struct queue exp_repl, result;

	args = exp_args = NULL;
	num_args = 0;

	queue_init(&exp_repl, cpp_token_delete);
	queue_init(&result, cpp_token_delete);

	/* repl-list-end-marker */
	lexer_token_init(&_repl_list_end);
	repl_list_end.base = &_repl_list_end;;
	repl_list_end.base->type = LXR_TOKEN_REPL_LIST_END;

	err = cpp_token_stream_remove_head(stream, &ident);
	if (err)
		return err;

	/* The ident token donates its npws to the first token of the exp_repl. */
	has_white_space = cpp_token_has_white_space(ident);

	/* non-idents require no macro processing */
	/* Marked tokens are not expanded */
	is_ident = cpp_token_is_identifier(ident);
	is_marked = cpp_token_is_marked(ident);
	if (!is_ident || is_marked)
		return queue_add_tail(out, ident);

	/* non-macro idents require no macro processing */
	/* If the ident matches with an active macro, mark it and return */
	name = cpp_token_resolved(ident);
	macro = scanner_find_macro(this, name);
	is_macro = macro != NULL;
	is_active = is_macro && stack_find(mstk, macro);
	if (is_active)
		ident->is_marked = true;
	if (!is_macro || is_active)
		return queue_add_tail(out, ident);

	repl = &macro->replacement_list;

	/* object-like. */
	if (macro->is_function_like == false) {
		cpp_token_delete(ident);
		err = cpp_tokens_expand_object_like(repl, &exp_repl);
		goto rescan;
	}

	/* ident is not freed yet */

	/* func-like */
	err = cpp_token_stream_peek_head(stream, &token);
	if (err && err != EOF)
		return err;

	/*
	 * macro is func-like. But the next token is neither left-paren
	 * nor caller's repl-list-end marker. If the next token were left-paren,
	 * then this is an invocation. If the next token were repl-list-end-marker,
	 * then there is hope that the tokens beyond the marker may be able to
	 * proivide an invocation. In the absence of both these types of tokens, we
	 * can safely say that this is NOT an invocation of the func-like macro,
	 * and output the ident.
	 */
	if (!err && cpp_token_type(token) == LXR_TOKEN_REPL_LIST_END) {
		err = cpp_token_stream_remove_head(stream, &token);
		assert(err == ESUCCESS);
		/* Don't free the token; it is allocated on the stack of the caller */
		/* Return EPARTIAL to the call that placed the barrier */
		err = cpp_token_stream_add_head(stream, ident);
		if (!err)
			err = EPARTIAL;
		return err;
	}

	if (err || cpp_token_type(token) != LXR_TOKEN_LEFT_PAREN)
		return queue_add_tail(out, ident);

	/*
	 * place ident back at the head position.
	 * We need to keep ident and left-paren at their positions, in case this
	 * turns out to be a partial invocation.
	 */
	err = cpp_token_stream_add_head(stream, ident);
	if (err)
		return err;

	/*
	 * invocation is func-like too.
	 * We may not be able to collect arguments if the stream is partial.
	 * In that case, fall back to the caller.
	 */
	err = cpp_token_stream_collect_arguments(stream, macro, &args, &num_args);
	if (err)
		return err;	/* including partial invocation */

	/*
	 * If no error in collecting, the ident, left-paren, args and right-paren
	 * are all consumed.
	 */
	if (num_args) {
		exp_args = malloc(num_args * sizeof(exp_args[0]));
		if (exp_args == NULL)
			return ENOMEM;

		for (i = 0; i < num_args; ++i) {
			queue_init(&exp_args[i], cpp_token_delete);

			/* empty arg */
			if (queue_num_entries(&args[i]) == 0)
				continue;

			err = scanner_expand_argument(this, &args[i], &exp_args[i]);
			assert(err == ESUCCESS);
			if (err)
				return err;
		}
	}

	/* need to send all, since va_opt may require */
	err = scanner_expand_function_like(this, macro, args, exp_args, num_args,
									   &exp_repl);
	/* Come here to rescan after the expanded repl-list is built */
rescan:
	if (err)
		return err;

	/*
	 * The first token of a repl-list has its npws set to 0, as that
	 * npws is immaterial. After the substitution, the first token of the
	 * exp_repl must get its white-space property from the invocation, i.e.
	 * from ident.
	 *
	 * This fixes the "char p[] = x ## y" example of the c2x standard.
	 */
	if (!queue_is_empty(&exp_repl)) {
		token = queue_peek_head(&exp_repl);
		token->has_white_space = has_white_space;
		cpp_tokens_remove_place_markers(&exp_repl);
	}

	if (queue_is_empty(&exp_repl))
		goto done;

	err = stack_push(mstk, (void *)macro);
	if (err)
		return err;

	/* Add exp-repl + repl-end-marker tokens to the stream's front */
	err = cpp_token_stream_add_head(stream, &repl_list_end);
	if (err)
		return err;

	queue_for_each_with_rem_rev(&exp_repl, token) {
		err = cpp_token_stream_add_head(stream, token);
		if (err)
			return err;
	}

	while (true) {
		/* We do not expect an error reading from the stream */
		err = cpp_token_stream_peek_head(stream, &token);
		assert(err == ESUCCESS);

		/* At the end of our influence. Delete the marker. */
		if (token == &repl_list_end) {
			err = cpp_token_stream_remove_head(stream, &token);
			assert(err == ESUCCESS);
			break;
		}
		err = scanner_process_one(this, mstk, stream, &result);
		if (err == EPARTIAL) {
			assert(queue_is_empty(&result));
			assert(!cpp_token_stream_is_empty(stream));
			/*
			 * Our barrier was signalled. It was removed. Return success to our
			 * caller so that it may continue forward.
			 */
			err = ESUCCESS;
			break;
		}
		if (!err)
			err = queue_move(&result, out);
		if (err)
			break;
	}
	m = stack_pop(mstk);
	assert(m == macro);
done:
	for (i = 0; i < num_args; ++i) {
		queue_empty(&args[i]);
		queue_empty(&exp_args[i]);
	}
	if (num_args) {
		free(args);
		free(exp_args);
	}
	return err;
}
/*****************************************************************************/
#if 0
static
err_t scanner_serialize_cpp_token(struct scanner *this,
								  const struct cpp_token *token)
{
	off_t string_len;
	int ret;
	size_t size;
	const void *buf;
	const struct lexer_token *base;
	enum lexer_token_type type;

	/* First write the lexer_token type */
	base = token->base;
	type = cpp_token_type(base);
	buf = &type;
	size = sizeof(type);
	ret = write(this->cpp_tokens_fd, buf, size);
	if (ret < 0)
		return errno;

	/* Then write string_len */
	string_len = lexer_token_string_length(base);
	buf = &string_len;
	size = sizeof(string_len);
	ret = write(this->cpp_tokens_fd, buf, size);
	if (ret < 0)
		return errno;

	/* If the string_len is not 0, then write the string. */
	if (string_len == 0)
		return ESUCCESS;

	buf = NULL;
	if (type == LXR_TOKEN_UTF_16_STRING_LITERAL ||
		type == LXR_TOKEN_UTF_16_CHAR_CONST) {
		buf = lexer_token_utf16_string(base);
		size = string_len * 2;
	} else if (type == LXR_TOKEN_UTF_32_STRING_LITERAL ||
			   type == LXR_TOKEN_WCHAR_T_STRING_LITERAL ||
			   type == LXR_TOKEN_UTF_32_CHAR_CONST ||
			   type == LXR_TOKEN_WCHAR_T_CHAR_CONST) {
		buf = lexer_token_utf32_string(base);
		size = string_len * 4;
	} else {
		buf = lexer_token_string(base);
		size = string_len;
	}

	assert(buf);

	ret = write(this->cpp_tokens_fd, buf, size);
	if (ret < 0)
		return errno;
	return ESUCCESS;
}

static
err_t scanner_serialize_cpp_tokens(struct scanner *this,
								   struct cpp_token **inout_prev,
								   struct queue *tokens)
{
	err_t err;
	struct cpp_token *curr;
	struct cpp_token *prev;
	enum lexer_token_type type[2];

	err = ESUCCESS;
	prev = *inout_prev;
	while (!queue_is_empty(tokens)) {
		curr = queue_remove_head(tokens);

		/* See if the prev and curr tokens can be merged */
		if (prev &&
			lexer_token_is_string_literal(prev->base) &&
			lexer_token_is_string_literal(curr->base)) {
			type[0] = cpp_token_type(prev->base);
			type[1] = cpp_token_type(curr->base);

			/*
			 * There strings are compatible if:
			 * (1) they are the same type, or
			 * (2) at least one is char-const-string-literal
			 *
			 * If they are the same type, just join them.
			 * If one of them is char-const-str-literal, convert that literal
			 * to the other type.
			 */
			if (type[0] == type[1]) {
				err = lexer_token_concat_strings(prev->base, curr->base);
				if (err)
					return err;
				cpp_token_delete(curr);
				continue;
			}

			/* If any one of them is utf-8/char-str, then convert */
			if (type[0] == LXR_TOKEN_CHAR_STRING_LITERAL ||
				type[0] == LXR_TOKEN_UTF_8_STRING_LITERAL ||
				type[1] == LXR_TOKEN_CHAR_STRING_LITERAL ||
				type[1] == LXR_TOKEN_UTF_8_STRING_LITERAL) {
				err = lexer_token_concat_strings(prev->base, curr->base);
				if (err)
					return err;
				cpp_token_delete(curr);
				continue;
			}
			/* Cannot be reconciled. fall thru. */
		}

		if (prev == NULL) {
			prev = curr;
			continue;
		}

		/* write prev */
		err = scanner_serialize_cpp_token(this, prev);
		if (err)
			return err;

		/* make curr the prev. */
		prev = curr;
	}
	*inout_prev = prev;
	return err;
}
#endif
static
err_t scanner_serialize_cpp_token(struct scanner *this,
								  const struct cpp_token *token)
{
	size_t src_len;
	int ret;
	size_t size;
	const void *buf;
	enum lexer_token_type type;

	/* First write the lexer_token type */
	type = cpp_token_type(token);
	buf = &type;
	size = sizeof(type);
	ret = write(this->cpp_tokens_fd, buf, size);
	if (ret < 0)
		return errno;

	/*
	 * If the type is identifier, write its resolved source.
	 * Note that strings and char-consts have not been resolved yet, because
	 * they do not need to at the cpp stage. Only those char-consts subjected
	 * to #if constructs are evaluated, but otherwise the char-consts are not
	 * resolved.
	 */
	src_len = cpp_token_source_length(token);
	buf = cpp_token_source(token);
	if (type == LXR_TOKEN_IDENTIFIER &&
		cpp_token_resolved(token) != cpp_token_source(token)) {
		src_len = cpp_token_resolved_length(token);
		buf = cpp_token_resolved(token);
	}

	/* Then write source_len */
	buf = &src_len;
	size = sizeof(src_len);
	ret = write(this->cpp_tokens_fd, buf, size);
	if (ret < 0)
		return errno;

	/* If the source_len is not 0, then write the source. */
	if (src_len == 0)
		return ESUCCESS;

	size = src_len;
	assert(buf);
	ret = write(this->cpp_tokens_fd, buf, size);
	if (ret < 0)
		return errno;
	return ESUCCESS;
}

/* out is initialized by the caller */
static
err_t cpp_token_stream_scan_line(struct cpp_token_stream *this,
								 struct queue *out)
{
	err_t err;
	struct cpp_token *token;
	struct lexer_position save;

	assert(this->lexer);
	while (true) {
		save = lexer_position(this->lexer);
		err = cpp_token_stream_remove_head(this, &token);
		if (err)
			break;

		/* We have reached a token on the next line. push it back. */
		if (cpp_token_is_first(token)) {
			cpp_token_delete(token);
			lexer_set_position(this->lexer, save);
			break;
		}
		err = queue_add_tail(out, token);
		if (err)
			return err;
	}

	if (err == EOF)
		err = ESUCCESS;
	if (err)
		return err;
#if 1
	{
		int num_tokens, i;

		printf("%s: '#", __func__);
		num_tokens = queue_num_entries(out);
		for (i = 0; i < num_tokens; ++i) {
			token = queue_peek_entry(out, i);
			if (cpp_token_has_white_space(token))
				printf(" ");
			printf("%s", cpp_token_source(token));
		}
		printf("'\n");
	}
#endif
	return err;
}

static
err_t scanner_scan_file(struct scanner *this,
						const char *path)
{
	int i;
	err_t err;
	struct queue output, line;
	struct queue mstk;
	struct cpp_token_stream stream;
	struct lexer *lexer;
	struct cpp_token *token;
	static int depth = -1;	/* file inclusion depth */

	++depth;
	printf("%s[%d]: %s\n", __func__, depth, path);

	/* path instead of buf/buf-size */
	err = lexer_new(path, NULL, 0, &lexer);
	if (err)
		goto err0;

	if (depth == 0 && lexer_buffer_size(lexer) == 0) {
		/* The src file to compile must not be empty */
		err = EINVAL;
		goto err1;
	}

	stack_init(&mstk);
	cpp_token_stream_init(&stream, lexer);
	queue_init(&line, cpp_token_delete);
	queue_init(&output, cpp_token_delete);
	while (true) {
		err = cpp_token_stream_remove_head(&stream, &token);
		if (err) {
			if (err == EOF)
				err = ESUCCESS;
			break;
		}
		/*
		 * Directive must have the # as the first non-white-space token on a
		 * new line.
		 */
		if (cpp_token_type(token) == LXR_TOKEN_HASH &&
			cpp_token_is_first(token)) {
			cpp_token_delete(token);
			if (!err)
				err = cpp_token_stream_scan_line(&stream, &line);
			if (!err)
				err = scanner_scan_directive(this, &line, lexer->dir_path);
			queue_empty(&line);
			if (err)
				break;
			continue;
		}

		if (cond_incl_stack_in_skip_zone(&this->cistk)) {
			cpp_token_delete(token);
			continue;
		}

		/* add potential identifier back into the stream */
		err = cpp_token_stream_add_head(&stream, token);
		if (err)
			break;

		/*
		 * We do not add a barrier here after the token. Hence we do not expect
		 * to see a EPARTIAL error.
		 */
		do {
			err = scanner_process_one(this, &mstk, &stream, &output);
			assert(err != EPARTIAL);
		} while (!err && !cpp_token_stream_is_empty(&stream));
		if (err)
			break;
		assert(cpp_token_stream_is_empty(&stream));

		/*
		 * Note that some tokens may have no whitespace preceding it. In that
		 * case, accidental pasting may be seen in the output. However, only
		 * this print loop is affected. When the tokens are serialized, and
		 * then processed, they remain separate as they are now.
		 */
		printf("%s:", __func__);
		queue_for_each(&output, i, token) {
			if (cpp_token_has_white_space(token))
				printf(" ");
			printf("%s", cpp_token_source(token));
		}
		printf("\n");
		queue_for_each_with_rem(&output, token) {
			err = scanner_serialize_cpp_token(this, token);
			if (err)
				break;
			cpp_token_delete(token);
		}
		/*queue_empty(&output);*/
		assert(queue_is_empty(&output));
	}
#if 0
	if (!err && prev) {
		err = scanner_serialize_cpp_token(this, prev);
		cpp_token_delete(prev);
	}
#endif
	printf("%s[%d]: %s ends with %d\n", __func__, depth, path, err);
err1:
	lexer_delete(lexer);
err0:
	--depth;
	assert(!err);
	return err;
}

err_t scanner_scan_predefined_macros(struct scanner *this)
{
	int fd, ret;
	const char *path;
	err_t err;
	static const char *macros =
		"#define __STDC__ 1\n"
		"#define __STDC_EMBED_NOT_FOUND__ 0\n"
		"#define __STDC_EMBED_FOUND__ 1\n"
		"#define __STDC_EMBED_EMPTY__ 2\n"
		"#define __STDC_HOSTED__ 1\n"
		"#define __STDC_UTF_16__ 1\n"
		"#define __STDC_UTF_32__ 1\n"
		"#define __STDC_VERSION__ 202311L\n"
		"#define __STDC_NO_ATOMICS__ 1\n"
		"#define __STDC_NO_COMPLEX__ 1\n"
		"#define __STDC_NO_THREADS__ 1\n"
		"#define __STDC_NO_VLA__ 1\n"

		/*
		 * glibc's stdc-predef.h already defines __STDC_ISO_10646__ to
		 * 201706L. Inserting the below definition results in violation of
		 * macro identity. For now, go along with glibc's definition, as we are
		 * also relying on is headers for testing.
		 */
#if 0
		"#define __STDC_ISO_10646__ 202012L\n"
#endif
		/* These declarations to allow parsing GNU headers. */
		"#define __x86_64__ 1\n";

	err = mkstemp(&fd, &path);
	if (err)
		goto err0;

	ret = write(fd, macros, strlen(macros));
	if (ret < 0) {
		err = errno;
		goto err1;
	}
	err = scanner_scan_file(this, path);
err1:
	close(fd);
	unlink(path);
	free((void *)path);
err0:
	return err;
}

err_t scanner_scan(struct scanner *this,
				   const char *path)
{
	err_t err;

	err = scanner_scan_predefined_macros(this);
	this->is_running_predefined_macros = false;
	if (!err)
		err = scanner_scan_file(this, path);
	return err;
}
