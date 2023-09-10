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
static
err_t cc_parse_node_visit(struct cc_parse_node *this,
						  struct cc_parse_node *out);	/* parent ast node */
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

static
void cc_token_print(const struct cc_token *this)
{
	const char *type = g_cc_token_type_str[this->type];
	printf("%s: %s", __func__, type);
	if (this->string)
		printf(" '%s'", this->string);
	printf("\n");
}
/*****************************************************************************/
void cc_grammar_rule_delete(void *p)
{
	struct cc_grammar_rule *this = p;
	free(this->elements.entries);
}

static inline
void cc_grammar_element_delete(void *p)
{
	struct cc_grammar_element *this = p;
	valq_empty(&this->rules);
}

void cc_grammar_item_delete(void *p)
{
	free(p);
}

static
void cc_grammar_item_set_delete(void *p)
{
	struct cc_grammar_item_set *this = p;
	ptrq_empty(&this->reduce_items);
	ptrq_empty(&this->shift_items);
	/*
	 * For successful parses, this->token is removed from the item-set and made
	 * part of the parse tree. Until the parse-tree is built, this function is
	 * responsible for deleting the token.
	 */
	if (this->token)
		cc_token_delete(this->token);
}

/* Building ast out of parse-tree will nullify certain token ptrs. TODO */
void cc_parse_node_delete(void *p)
{
	struct cc_parse_node *this = p;
	if (!cc_token_type_is_non_terminal(this->type)) {
		/* The token may have been reassigned to an ast-node. */
		if (this->token)
			cc_token_delete(this->token);
	} else {
		assert(this->token == NULL);
	}
	ptrq_empty(&this->child_nodes);
	free(this);
}
/*****************************************************************************/
static
bool
cc_grammar_item_set_find_item(const struct cc_grammar_item_set *this,
							  const bool is_shift_item,
							  const struct cc_grammar_item *item)
{
	int i;
	const struct cc_grammar_item *t;
	const struct ptr_queue *q;

	q = &this->reduce_items;
	if (is_shift_item)
		q = &this->shift_items;
	PTRQ_FOR_EACH(q, i, t) {
		if (t->element != item->element)
			continue;
		if (t->rule != item->rule)
			continue;
		if (t->dot_position != item->dot_position)
			continue;
		if (t->origin != item->origin)
			continue;
		return true;
	}
	return false;
}

static
err_t cc_grammar_item_set_add_shift_item(struct cc_grammar_item_set *this,
										 struct cc_grammar_item *item)
{
	if (cc_grammar_item_set_find_item(this, true, item))
		return EEXIST;
	return ptrq_add_tail(&this->shift_items, item);
}

static
err_t cc_grammar_item_set_add_reduce_item(struct cc_grammar_item_set *this,
										  struct cc_grammar_item *item)
{
	if (cc_grammar_item_set_find_item(this, false, item))
		return EEXIST;
	return ptrq_add_tail(&this->reduce_items, item);
}

static
err_t cc_grammar_item_set_add_item(struct cc_grammar_item_set *this,
								   struct cc_grammar_item *item)

{
	err_t err;
	int num_rules, num_rhs;
	const struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;

	ge = item->element;
	assert(ge);
	assert(cc_token_type_is_non_terminal(ge->type));

	num_rules = valq_num_entries(&ge->rules);
	assert(num_rules);
	assert(0 <= item->rule && item->rule < num_rules);
	gr = valq_peek_entry(&ge->rules, item->rule);
	assert(gr);
	num_rhs = valq_num_entries(&gr->elements);
	assert(num_rhs);

	if (item->dot_position == num_rhs)
		err = cc_grammar_item_set_add_reduce_item(this, item);
	else
		err = cc_grammar_item_set_add_shift_item(this, item);
	if (err == EEXIST)
		cc_grammar_item_delete(item);
	return err;
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
	valq_init(&this->elements, sizeof(struct cc_grammar_element),
			  cc_grammar_element_delete);
	valq_init(&this->item_sets, sizeof(struct cc_grammar_item_set),
			  cc_grammar_item_set_delete);
	err = cc_load_grammar(this);
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
	valq_empty(&this->elements);
	valq_empty(&this->item_sets);
}

err_t compiler_delete(struct compiler *this)
{
	assert(this);
	cc_parse_node_delete(this->root);
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
	static size_t index = 0;

	if (index >= this->buffer_size)
		return EOF;

	assert(index < this->buffer_size);
	assert(sizeof(type) == 4);
	memcpy(&type, &this->buffer[index], sizeof(type));
	index += sizeof(type);
	token = malloc(sizeof(*token));
	if (token == NULL)
		return ENOMEM;
	token->type = type;

	/* key-words and punctuators do not have a src-len. */
	if (cc_token_type_is_key_word(type)) {
		token->string = g_key_words[type - CC_TOKEN_ATOMIC];
		token->string_len = strlen(token->string);
		*out = token;
		return ESUCCESS;
	} else if (cc_token_type_is_punctuator(type)) {
		token->string = g_punctuators[type - CC_TOKEN_LEFT_BRACE];
		token->string_len = strlen(token->string);
		*out = token;
		return ESUCCESS;
	}
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
	token->string = src;
	token->string_len = src_len;
	*out = token;
	return ESUCCESS;
}

static
err_t cc_token_stream_peek_head(struct cc_token_stream *this,
								struct cc_token **out)
{
	err_t err;

	if (!cc_token_stream_is_empty(this)) {
		*out = ptrq_peek_head(&this->q);
		return ESUCCESS;
	}

	/* Not reading from the cpp_tokens file? EOF */
	if (this->buffer == NULL)
		return EOF;
	err = cc_token_stream_read_token(this, out);
	if (!err)
		err = cc_token_convert(*out);
	if (!err)
		err = ptrq_add_tail(&this->q, *out);
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
	token = ptrq_remove_head(&this->q);
	assert(token == *out);
	return err;
}
/*****************************************************************************/
static
void cc_grammar_item_print(const struct cc_grammar_item *this)
{
	int i, num_rhs;
	const struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;
	const enum cc_token_type *ptype;
	enum cc_token_type type;

	ge = this->element;
	printf("[%s ->", &g_cc_token_type_str[ge->type][strlen("CC_TOKEN_")]);
	gr = valq_peek_entry(&ge->rules, this->rule);
	num_rhs = valq_num_entries(&gr->elements);
	for (i = 0; i < num_rhs; ++i) {
		ptype = valq_peek_entry(&gr->elements, i);
		type = *ptype;
		if (this->dot_position == i)
			printf(" .");
		/* If type is a key-word or punctuator, print it */
		if (type >= CC_TOKEN_ATOMIC && type <= CC_TOKEN_WHILE)
			printf(" %s", g_key_words[type - CC_TOKEN_ATOMIC]);
		else if (type >= CC_TOKEN_LEFT_BRACE && type <= CC_TOKEN_ELLIPSIS)
			printf(" %s", g_punctuators[type - CC_TOKEN_LEFT_BRACE]);
		else
			printf(" %s", &g_cc_token_type_str[type][strlen("CC_TOKEN_")]);
		if (!cc_token_type_is_non_terminal(type))
			continue;
		if (i != this->dot_position - 1)
			continue;
		assert(this->back);
		printf("(%d,%d)", this->back_item_set, this->back_item);
	}
	if (this->dot_position == i)
		printf(" .");
	printf("] (%d)", this->origin);
	printf("\n");
}

void cc_grammar_item_set_print(const struct cc_grammar_item_set *set)
{
	int num_items, i;
	const struct ptr_queue *q;

	q = &set->reduce_items;
	num_items = ptrq_num_entries(q);
	printf("item-set[%4d]:r[%4d]----------------------\n", set->index,
		   num_items);
	for (i = 0; i < num_items; ++i) {
		printf("[%4d]:", i);
		cc_grammar_item_print(ptrq_peek_entry(q, i));
	}

	q = &set->shift_items;
	num_items = ptrq_num_entries(q);
	printf("item-set[%4d]:s[%4d]----------------------\n", set->index,
		   num_items);
	for (i = 0; i < num_items; ++i) {
		printf("[%4d]:", i);
		cc_grammar_item_print(ptrq_peek_entry(q, i));
	}

	printf("item-set[%4d]:done-------------------------\n", set->index);
	printf("\n");
}

static
err_t compiler_run_scanning(struct compiler *this,
							const struct cc_grammar_item_set *set,
							struct cc_token *token)
{
	int num_items_added, num_items, num_rhs, i;
	struct cc_grammar_item *ti;
	struct cc_grammar_item_set ts;
	const struct cc_grammar_item *item;
	const struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;
	const enum cc_token_type *ptype;
	enum cc_token_type type;
	err_t err;

	/* The token is a non-terminal scanned from index */
	assert(!cc_token_type_is_non_terminal(token->type));

	cc_grammar_item_set_init(&ts, set->index + 1);

	/*
	 * Find items in input-set set that have a dot in front of the terminal
	 * token, and add that item with dot-pos moved.
	 */
	num_items_added = 0;
	num_items = ptrq_num_entries(&set->shift_items);
	for (i = 0; i < num_items; ++i) {
		item = ptrq_peek_entry(&set->shift_items, i);
		ge = item->element;
		gr = valq_peek_entry(&ge->rules, item->rule);
		num_rhs = valq_num_entries(&gr->elements);
		assert(item->dot_position < num_rhs);	/* bcoz shift-item */
		ptype = valq_peek_entry(&gr->elements, item->dot_position);
		assert(ptype);
		type = *ptype;
		if (type != token->type)
			continue;
		ti = malloc(sizeof(*ti));
		if (ti == NULL)
			return ENOMEM;
		*ti = *item;
		ti->back = NULL;	/* dot is after a terminal. */
		++ti->dot_position;
		err = cc_grammar_item_set_add_item(&ts, ti);
		if (!err)
			++num_items_added;
		if (err && err != EEXIST)
			return err;
	}
	assert(num_items_added);
	return valq_add_tail(&this->item_sets, &ts);
}

static
err_t compiler_run_completion(const struct compiler *this,
							  struct cc_grammar_item_set *set)
{
	int num_items_added, num_items[2], num_rhs, index, i, j, num_rules;
	struct cc_grammar_item *ti;
	const struct cc_grammar_item *items[3];
	const struct cc_grammar_item_set *sets[3];
	const struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;
	const enum cc_token_type *ptype;
	enum cc_token_type type;
	err_t err;
	bool set_changed;

	sets[0] = set;
	num_items_added = 0;
	do {
		set_changed = false;
		num_items[0] = ptrq_num_entries(&sets[0]->reduce_items);
		for (i = 0; i < num_items[0]; ++i) {
			items[0] = ptrq_peek_entry(&sets[0]->reduce_items, i);
			sets[1] = valq_peek_entry(&this->item_sets, items[0]->origin);
			assert(sets[1]);
			assert(sets[1]->index == items[0]->origin);
			/*
			 * Find all items in origin set gis that contains a dot in front of
			 * the items[0]->element. If found, add that item to set with dot
			 * moved one step.
			 */
			num_items[1] = ptrq_num_entries(&sets[1]->shift_items);
			for (j = 0; j < num_items[1]; ++j) {
				items[1] = ptrq_peek_entry(&sets[1]->shift_items, j);
				ge = items[1]->element;
				assert(ge);
				num_rules = valq_num_entries(&ge->rules);
				assert(num_rules);
				assert(items[1]->rule < num_rules);
				gr = valq_peek_entry(&ge->rules, items[1]->rule);
				assert(gr);
				num_rhs = valq_num_entries(&gr->elements);
				assert(num_rhs);
				assert(items[1]->dot_position < num_rhs);
				ptype = valq_peek_entry(&gr->elements, items[1]->dot_position);
				type = *ptype;
				if (!cc_token_type_is_non_terminal(type))
					continue;
				index = type - CC_TOKEN_TRANSLATION_OBJECT;
				ge = valq_peek_entry(&this->elements, index);
				if (ge != items[0]->element)
					continue;
				ti = malloc(sizeof(*ti));
				if (ti == NULL)
					return ENOMEM;
				*ti = *items[1];
				++ti->dot_position;
				ti->back = items[0];
				ti->back_item_set = sets[0]->index;
				ti->back_item = i;
				err = cc_grammar_item_set_add_item(set, ti);
				if (!err) {
					++num_items_added;
					set_changed = true;
				}
				if (err && err != EEXIST)
					return err;
			}
		}
	} while (set_changed);
	if (num_items_added == 0)
		return EEXIST;
	return ESUCCESS;
}

static
err_t compiler_run_prediction(const struct compiler *this,
							  struct cc_grammar_item_set *set)
{
	int num_items_added, num_items, num_rhs, index, i, j, num_rules;
	struct cc_grammar_item *ti;
	const struct cc_grammar_item *item;
	const struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;
	const enum cc_token_type *ptype;
	enum cc_token_type type;
	err_t err;
	bool set_changed;

	num_items_added = 0;
	do {
		set_changed = false;
		num_items = ptrq_num_entries(&set->shift_items);
		for (i = 0; i < num_items; ++i) {
			item = ptrq_peek_entry(&set->shift_items, i);
			ge = item->element;
			gr = valq_peek_entry(&ge->rules, item->rule);
			num_rhs = valq_num_entries(&gr->elements);
			assert(item->dot_position < num_rhs);	/* bcoz shift-item */
			ptype = valq_peek_entry(&gr->elements, item->dot_position);
			assert(ptype);
			type = *ptype;
			if (!cc_token_type_is_non_terminal(type))
				continue;
			index = type - CC_TOKEN_TRANSLATION_OBJECT;
			ge = valq_peek_entry(&this->elements, index);
			assert(ge->type == type);
			num_rules = valq_num_entries(&ge->rules);
			for (j = 0; j < num_rules; ++j) {
				ti = calloc(1, sizeof(*ti));
				if (ti == NULL)
					return ENOMEM;
				ti->element = ge;
				ti->rule = j;
				ti->origin = set->index;
				err = cc_grammar_item_set_add_item(set, ti);
				if (!err) {
					++num_items_added;
					set_changed = true;
				}
				if (err && err != EEXIST)
					return err;
			}
		}
	} while (set_changed);
	if (num_items_added == 0)
		return EEXIST;
	return ESUCCESS;
}

static
err_t compiler_parse(struct compiler *this)
{
	int index;
	bool set_changed;
	err_t err;
	struct cc_grammar_item_set *set;
	struct cc_token *token;
	struct cc_grammar_item *item;
	struct cc_grammar_item_set ts;

	/* Prepare the item-set-#0 */
	item = calloc(1, sizeof(*item));
	if (item == NULL)
		return ENOMEM;
	cc_grammar_item_set_init(&ts, 0);
	item->element = valq_peek_entry(&this->elements, 0);
	err = cc_grammar_item_set_add_item(&ts, item);
	assert(err != EEXIST);
	if (!err)
		err = valq_add_tail(&this->item_sets, &ts);
	if (err)
		return err;

	index = 0;
	token = NULL;
	while (true) {
		set = valq_peek_entry(&this->item_sets, index);
		assert(set);
		if (token) {
			set->token = token;
			cc_token_print(token);
		}
		do {
			set_changed = false;
			/*
			 * The functions return EEXIST if no change is made to the set,
			 * ESUCCESS if set is changed, and any other error on error.
			 */
			err = compiler_run_prediction(this, set);
			set_changed = !err ? true : set_changed;
			if (err && err != EEXIST)
				return err;
			err = compiler_run_completion(this, set);
			set_changed = !err ? true : set_changed;
			if (err && err != EEXIST)
				return err;
		} while (set_changed);

		cc_grammar_item_set_print(set);
		err = cc_token_stream_remove_head(&this->stream, &token);
		if (err) {
			err = err == EOF ? ESUCCESS : err;
			break;
		}
		err = compiler_run_scanning(this, set, token);
		if (err)
			return err;
		++index;
	}
	return err;
}
/*****************************************************************************/
static
err_t compiler_calc_back_info(const struct compiler *this,
							  struct cc_grammar_item_set *set,
							  const struct cc_grammar_item *item,
							  struct val_queue *back_infos)
{
	err_t err;
	int dot_position, origin, i;
	struct cc_grammar_back_info bi;
	const struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;
	const struct cc_grammar_item *curr;
	const enum cc_token_type *ptype;
	enum cc_token_type type;

	printf("%s:", __func__);
	cc_grammar_item_print(item);
	/*
	 * The curr is an item of the type A -> B C(x,y) . D, where C is a
	 * non-terminal.
	 */
	curr = item;
	dot_position = curr->dot_position;
	assert(dot_position);
	while (dot_position) {
		ge = curr->element;
		gr = valq_peek_entry(&ge->rules, curr->rule);
		/*
		 * If there's a non-terminal to the left of the dot-pos, then back must
		 * be valid.
		 */
		--dot_position;
		ptype = valq_peek_entry(&gr->elements, dot_position);
		assert(ptype);
		type = *ptype;
		bi.token = NULL;
		if (!cc_token_type_is_non_terminal(type)) {
			assert(set->token);
			assert(set->token->type == type);
			bi.token = set->token;
			set->token = NULL;
			bi.item_set = set->index - 1;
			bi.item = EOF;
		} else {
			/* non-terminal left of dot. Hence back must be valid */
			assert(curr->back);
			bi.item_set = curr->back_item_set;
			bi.item = curr->back_item;
		}
		err = valq_add_head(back_infos, &bi);
		if (err)
			return err;
		if (dot_position == 0)
			break;	/* Can't have left-dot-neighbour */

		/*
		 * A -> B . C D (k)
		 * Find the left-dot-neighbour. In the origin of the back-item,
		 * there should be an item of type A -> B(x,y) . C D (k)
		 */
		if (!cc_token_type_is_non_terminal(type)) {
			/* bi.item_set must have an item */
			origin = bi.item_set;
			set = valq_peek_entry(&this->item_sets, bi.item_set);
		} else {
			set = valq_peek_entry(&this->item_sets, curr->back_item_set);
			item = ptrq_peek_entry(&set->reduce_items, curr->back_item);
			assert(item);
			origin = item->origin;
		}

		/*
		 * Find the item in origin of the same type as curr, but with
		 * dot-pos shifted to left. The item must be in shift-items
		 */
		set = valq_peek_entry(&this->item_sets, origin);
		PTRQ_FOR_EACH(&set->shift_items, i, item) {
			if (item->element != curr->element)
				continue;
			if (item->rule != curr->rule)
				continue;
			if (item->origin != curr->origin)
				continue;
			if (item->dot_position != dot_position)
				continue;
			break;
		}
		assert(item);
		curr = item;
	}
	assert(valq_num_entries(back_infos));
	return ESUCCESS;
}

static
err_t compiler_build_tree(struct compiler *this)
{
	err_t err;
	int i, num_rhs;
	const struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;
	const struct cc_grammar_item *item;
	struct cc_grammar_item_set *set;
	const struct cc_grammar_back_info *bi;
	struct cc_parse_node *root, *node, *child;
	struct val_queue stack;
	struct val_queue back_infos;
	struct cc_parse_stack_entry entry;
	const enum cc_token_type *ptype;

	ge = valq_peek_entry(&this->elements, 0);
	i = valq_num_entries(&this->item_sets) - 1;
	set = valq_peek_entry(&this->item_sets, i);
	assert(set);

	PTRQ_FOR_EACH(&set->reduce_items, i, item) {
		if (item->element == ge)
			break;
	}
	if (item == NULL)
		return EINVAL;	/* Not a valid program */

	valq_init(&stack, sizeof(entry), NULL);
	valq_init(&back_infos, sizeof(*bi), NULL);

	err = compiler_calc_back_info(this, set, item, &back_infos);
	if (err)
		return err;
	assert(valq_num_entries(&back_infos));

	root = malloc(sizeof(*root));
	node = malloc(sizeof(*node));
	if (root == NULL || node == NULL)
		return ENOMEM;
	ptrq_init(&root->child_nodes, cc_parse_node_delete);
	ptrq_init(&node->child_nodes, cc_parse_node_delete);
	root->type = CC_TOKEN_TRANSLATION_OBJECT;
	node->type = CC_TOKEN_TRANSLATION_UNIT;
	root->token = node->token = NULL;
	err = ptrq_add_tail(&root->child_nodes, node);
	if (err)
		return err;

	assert(valq_num_entries(&back_infos) == 1);
	bi = valq_peek_entry(&back_infos, 0);
	entry.node = node;
	entry.back_item_set = bi->item_set;
	entry.back_item = bi->item;
	valq_remove_entry(&back_infos, 0);
	err = valq_add_tail(&stack, &entry);
	if (err)
		return err;

	while (!valq_is_empty(&stack)) {
		entry = *(struct cc_parse_stack_entry *)valq_peek_tail(&stack);
		valq_remove_tail(&stack);
		node = entry.node;
		set = valq_peek_entry(&this->item_sets, entry.back_item_set);
		item = ptrq_peek_entry(&set->reduce_items, entry.back_item);
		ge = item->element;
		gr = valq_peek_entry(&ge->rules, item->rule);
		num_rhs = valq_num_entries(&gr->elements);
		err = compiler_calc_back_info(this, set, item, &back_infos);
		if (err)
			return err;
		assert(valq_num_entries(&back_infos) == num_rhs);
		/* Add nodes for each element on the rhs */
		for (i = 0; i < num_rhs; ++i) {
			ptype = valq_peek_entry(&gr->elements, i);
			child = malloc(sizeof(*child));
			child->type = *ptype;
			ptrq_init(&child->child_nodes, cc_parse_node_delete);
			err = ptrq_add_tail(&node->child_nodes, child);
			if (err)
				return err;

			bi = valq_peek_head(&back_infos);
			child->token = bi->token;
			entry.back_item_set = bi->item_set;
			entry.back_item = bi->item;
			entry.node = child;
			valq_remove_head(&back_infos);
			if (!cc_token_type_is_non_terminal(child->type)) {
				assert(child->token);
				continue;	/* termianls need not be added on stack */
			}
			err = valq_add_tail(&stack, &entry);
			if (err)
				return err;
		}
	}
	this->root = root;
	return ESUCCESS;
}
/*****************************************************************************/
static
void cc_parse_node_print(const struct cc_parse_node *this)
{
	int i;
	const struct cc_parse_node *child;

	printf("\n(");
	if (!cc_token_type_is_non_terminal(this->type)) {
		assert(this->token);
		assert(this->token->string);
		printf("%s", this->token->string);
	} else {
		printf("%s", &g_cc_token_type_str[this->type][strlen("CC_TOKEN_")]);
	}
	PTRQ_FOR_EACH(&this->child_nodes, i, child)
		cc_parse_node_print(child);
	printf(")\n");
}

static
void compiler_print_parse_tree(const struct compiler *this)
{
	cc_parse_node_print(this->root);
}
/*****************************************************************************/
static
err_t cc_parse_node_visit_translation_object(struct cc_parse_node *this,
											 struct cc_parse_node *out)
{
	struct cc_parse_node *child;
	assert(cc_parse_node_type(out) == CC_TOKEN_TRANSLATION_UNIT);
	child = cc_parse_node_remove_head_child(this);
	cc_parse_node_delete(this);
	return cc_parse_node_visit(child, out);
}

static
err_t cc_parse_node_visit_translation_unit(struct cc_parse_node *this,
										   struct cc_parse_node *out)
{
	err_t err;
	struct cc_parse_node *child;

	/*
	 * CC_TOKEN_TRANSLATION_UNIT is left-associative.
	 * Visit in left-right-root order.
	 */
	assert(cc_parse_node_type(out) == CC_TOKEN_TRANSLATION_UNIT);
	child = cc_parse_node_remove_head_child(this);
	err = cc_parse_node_visit(child, out);
	if (err || cc_parse_node_num_children(this) == 0)
		return err;
	child = cc_parse_node_remove_head_child(this);
	return cc_parse_node_visit(child, out);
}

static
err_t cc_parse_node_visit_external_declaration(struct cc_parse_node *this,
											   struct cc_parse_node *out)
{
	struct cc_parse_node *child;
	assert(cc_parse_node_type(out) == CC_TOKEN_TRANSLATION_UNIT);
	child = cc_parse_node_peek_head_child(this);
	cc_parse_node_delete(this);
	return cc_parse_node_visit(child, out);
}

/*
 * Declaration can have these parents:
 *	ExternalDeclaration,
 *	IterationStatement (for loop)
 *	BlockItem
 *
 *	A declaration has two parts:
 *		DeclarationSpecifiers
 *		InitDeclaratorList
 *	DeclarationSpecifiers contain type and storage info, perhaps qualified.
 *	InitDeclaratorList is the location where the identifier is found.
 */
static
err_t cc_parse_node_visit_declaration(struct cc_parse_node *this,
									  struct cc_parse_node *out)
{
	err_t err;
	enum cc_token_type type;
	struct cc_parse_node *child;

	child = cc_parse_node_peek_head_child(this);
	type = cc_parse_node_type(child);
	if (type == CC_TOKEN_STATIC_ASSERT_DECLARATION ||
		type == CC_TOKEN_ATTRIBUTE_DECLARATION) {
		cc_parse_node_remove_head_child(this);
		return cc_parse_node_visit(child, out);
	}
	assert(0);
	return err;
}
#if 0
/*
 * StaticAssertDeclaration:
 *	single child, the ConstantExpression.
 *	token == string provided with the static-assert, if any.
 */
static
err_t cc_parse_node_visit_static_assert_declaration(struct cc_parse_node *this,
													struct cc_parse_node *out)
{
	int i;
	err_t err;
	struct cc_parse_node *child, *node;

	/* Delete parse-nodes for static-assert and left-paren */
	for (i = 0; i < 2; ++i) {
		child = cc_parse_node_remove_head_child(this);
		cc_parse_node_delete(child);
	}
	/* Delete parse-nodes for right-paren and semi-colon */
	for (i = 0; i < 2; ++i) {
		child = cc_parse_node_remove_tail_child(this);
		cc_parse_node_delete(child);
	}

	node = malloc(sizeof(*node));
	if (node == NULL)
		return ENOMEM;
	cc_parse_node_init(node, CC_TOKEN_STATIC_ASSERT_DECLARATION, NULL);
	child = cc_parse_node_remove_head_child(this);
	err = cc_parse_node_visit(child, node);
	if (err)
		return err;
	if (cc_parse_node_num_children(this) == 0)
		goto done;
	child = cc_parse_node_remove_tail_child(this);
	assert(child->token);
	assert(cc_token_is_string_literal(child->token));
	node->token = child->token;
	child->token = NULL;
done:
	return cc_parse_node_add_tail_child(out, node);
}

/* AttributeDeclaration: list of AttributeSpecifiers. */
static
err_t cc_parse_node_visit_attribute_declaration(struct cc_parse_node *this,
												struct cc_parse_node *out)
{
	err_t err;
	struct cc_parse_node *child, *node;

	node = malloc(sizeof(*node));
	if (node == NULL)
		return ENOMEM;
	cc_parse_node_init(node, CC_TOKEN_ATTRIBUTE_DECLARATION, NULL);
	child = cc_parse_node_remove_head_child(this);
	err = cc_parse_node_visit(child, node);
	if (!err)
		err = cc_parse_node_add_tail_child(out, node);
	return err;
}
#endif
static
err_t cc_parse_node_visit(struct cc_parse_node *this,
						  struct cc_parse_node *out)	/* parent ast node */
{
	err_t err;
	enum cc_token_type type;

	/* 'this' is a parse-tree node */
	err = ENOTSUP;
	type = cc_parse_node_type(this);
	if (type == CC_TOKEN_TRANSLATION_OBJECT)
		err = cc_parse_node_visit_translation_object(this, out);
	if (type == CC_TOKEN_TRANSLATION_UNIT)
		err = cc_parse_node_visit_translation_unit(this, out);
	if (type == CC_TOKEN_EXTERNAL_DECLARATION)
		err = cc_parse_node_visit_external_declaration(this, out);
	if (type == CC_TOKEN_DECLARATION)
		err = cc_parse_node_visit_declaration(this, out);
#if 0
	if (type == CC_TOKEN_STATIC_ASSERT_DECLARATION)
		err = cc_parse_node_visit_static_assert_declaration(this, out);
	if (type == CC_TOKEN_ATTRIBUTE_DECLARATION)
		err = cc_parse_node_visit_attribute_declaration(this, out);
#endif
	cc_parse_node_delete(this);
	return err;
}

static
err_t compiler_visit_parse_tree(struct compiler *this)
{
	struct cc_parse_node *root;

	root = malloc(sizeof(*root));
	if (root == NULL)
		return ENOMEM;
	cc_parse_node_init(root, CC_TOKEN_TRANSLATION_UNIT, NULL);
	return cc_parse_node_visit(this->root, root);
}
/*****************************************************************************/
err_t compiler_compile(struct compiler *this)
{
	err_t err;
	err = compiler_parse(this);
	if (!err)
		err = compiler_build_tree(this);
	if (!err)
		compiler_cleanup0(this);
	if (!err)
		compiler_print_parse_tree(this);
	if (false && !err)
		err = compiler_visit_parse_tree(this);
	assert(!err);
	return err;
}
