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
	ptrq_empty(&this->base_items);
	free(this->elements.entries);
}

static inline
void cc_grammar_element_delete(void *p)
{
	struct cc_grammar_element *this = p;
	valq_empty(&this->rules);
}

void cc_grammar_item_base_delete(void *p)
{
	struct cc_grammar_item_base *this = p;
	free(this->back.entries);
	free(this);
}

static
void cc_grammar_item_set_delete(void *p)
{
	struct cc_grammar_item_set *this = p;
	valq_empty(&this->reduce_items);
	valq_empty(&this->shift_items);
	/*
	 * For successful parses, this->token is removed from the item-set and made
	 * part of the parse tree. Until the parse-tree is built, this function is
	 * responsible for deleting the token.
	 */
	if (this->token)
		cc_token_delete(this->token);
}

void cc_parse_node_delete(void *p)
{
	struct cc_parse_node *this = p;
	cc_token_delete(this->token);
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
	struct cc_grammar_item ti;
	const struct val_queue *q;

	q = &this->reduce_items;
	if (is_shift_item)
		q = &this->shift_items;
	VALQ_FOR_EACH(q, i, ti) {
		if (ti.base != item->base)
			continue;
		if (ti.dot_position != item->dot_position)
			continue;
		return true;
	}
	return false;
}

static
err_t cc_grammar_item_set_add_shift_item(struct cc_grammar_item_set *this,
										 const struct cc_grammar_item *item)
{
	if (cc_grammar_item_set_find_item(this, true, item))
		return EEXIST;
	return valq_add_tail(&this->shift_items, item);
}

static
err_t cc_grammar_item_set_add_reduce_item(struct cc_grammar_item_set *this,
										  const struct cc_grammar_item *item)
{
	if (cc_grammar_item_set_find_item(this, false, item))
		return EEXIST;
	return valq_add_tail(&this->reduce_items, item);
}

static
err_t cc_grammar_item_set_add_item(struct cc_grammar_item_set *this,
								   const struct cc_grammar_item *item)

{
	int num_rules, num_rhs;
	const struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;
	const struct cc_grammar_item_base *base;

	base = item->base;
	assert(base);
	ge = base->element;
	assert(ge);
	assert(cc_token_type_is_non_terminal(ge->type));

	num_rules = valq_num_entries(&ge->rules);
	assert(num_rules);
	assert(0 <= base->rule && base->rule < num_rules);
	gr = valq_peek_entry(&ge->rules, base->rule);
	assert(gr);
	num_rhs = valq_num_entries(&gr->elements);
	assert(num_rhs);

	if (item->dot_position == num_rhs)
		return cc_grammar_item_set_add_reduce_item(this, item);
	return cc_grammar_item_set_add_shift_item(this, item);
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

err_t compiler_delete(struct compiler *this)
{
	assert(this);
	munmap((void *)this->stream.buffer, this->stream.buffer_size);
	close(this->cpp_tokens_fd);
	unlink(this->cpp_tokens_path);
	free((void *)this->cpp_tokens_path);
	cc_token_stream_empty(&this->stream);

	valq_empty(&this->elements);
	valq_empty(&this->item_sets);
	free(this);
	return ESUCCESS;
}
/*****************************************************************************/
/* Find base-item by comparing the actual contents */
static int max_num_base_items;
static
err_t cc_grammar_rule_find_base_item(const struct cc_grammar_rule *this,
									 const struct cc_grammar_item_base *base)
{
	int i;
	const struct cc_grammar_item_base *tb;

	/* TODO: Fix this Bottleneck */
	PTRQ_FOR_EACH(&this->base_items, i, tb) {
		if (tb->element != base->element)
			continue;
		if (tb->rule != base->rule)
			continue;
		if (tb->origin != base->origin)
			continue;
		return i;
	}
	return EOF;
}

static
err_t cc_grammar_rule_add_base_item(struct cc_grammar_rule *this,
									struct cc_grammar_item_base *base,
									struct cc_grammar_item_base **out)
{
	err_t err;
	int i;

	i = cc_grammar_rule_find_base_item(this, base);
	if (i == EOF) {
		i = ptrq_num_entries(&this->base_items);
		err = ptrq_add_tail(&this->base_items, base);
		if (err)
			return err;
		if (i + 1 > max_num_base_items)
			max_num_base_items = i + 1;
	} else {
		cc_grammar_item_base_delete(base);
	}
	assert(0 <= i && i < ptrq_num_entries(&this->base_items));
	*out = ptrq_peek_entry(&this->base_items, i);
	return ESUCCESS;
}

static
err_t cc_grammar_element_add_base_item(struct cc_grammar_element *this,
									   struct cc_grammar_item_base *base,
									   struct cc_grammar_item_base **out)
{
	struct cc_grammar_rule *gr;
	int num_rules;

	assert(base->element == this);
	num_rules = valq_num_entries(&this->rules);
	assert(base->rule < num_rules);
	gr = valq_peek_entry(&this->rules, base->rule);
	assert(gr);
	return cc_grammar_rule_add_base_item(gr, base, out);
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

	/* float-suffix without exp */
	if (str[i] == 'd' || str[i] == 'f' || str[i] == 'l' ||
		str[i] == 'D' || str[i] == 'F' || str[i] == 'L')
		goto floating_suffix;

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
	int i, num_rhs, index;
	const struct cc_grammar_derivation *derive;
	const struct cc_grammar_item_base *base;
	const struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;
	const enum cc_token_type *ptype;
	enum cc_token_type type;

	base = this->base;
	ge = base->element;
	printf("[%s ->", &g_cc_token_type_str[ge->type][strlen("CC_TOKEN_")]);
	gr = valq_peek_entry(&ge->rules, base->rule);
	num_rhs = valq_num_entries(&gr->elements);
	for (i = index = 0; i < num_rhs; ++i) {
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
		if (index >= valq_num_entries(&base->back))
			continue;
		/* Print the item-set# and the reduction rule */
		derive = valq_peek_entry(&base->back, index++);
		printf("(%d,%d)", derive->item_set, derive->item);
	}
	if (this->dot_position == i)
		printf(" .");
	printf("] (%d)", base->origin);
	printf("\n");
}

void cc_grammar_item_set_print(const struct cc_grammar_item_set *set)
{
	int num_items, i;
	const struct val_queue *q;

	q = &set->reduce_items;
	num_items = valq_num_entries(q);
	printf("item-set[%4d]:r[%4d]----------------------\n", set->index,
		   num_items);
	for (i = 0; i < num_items; ++i) {
		printf("[%4d]:", i);
		cc_grammar_item_print(valq_peek_entry(q, i));
	}

	q = &set->shift_items;
	num_items = valq_num_entries(q);
	printf("item-set[%4d]:s[%4d]----------------------\n", set->index,
		   num_items);
	for (i = 0; i < num_items; ++i) {
		printf("[%4d]:", i);
		cc_grammar_item_print(valq_peek_entry(q, i));
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
	struct cc_grammar_item ti;
	struct cc_grammar_item_set ts;
	const struct cc_grammar_item_base *base;
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
	num_items = valq_num_entries(&set->shift_items);
	for (i = 0; i < num_items; ++i) {
		item = valq_peek_entry(&set->shift_items, i);
		base = item->base;
		ge = base->element;
		gr = valq_peek_entry(&ge->rules, base->rule);
		num_rhs = valq_num_entries(&gr->elements);
		assert(item->dot_position < num_rhs);	/* bcoz shift-item */
		ptype = valq_peek_entry(&gr->elements, item->dot_position);
		assert(ptype);
		type = *ptype;
		if (type != token->type)
			continue;
		ti = *item;
		++ti.dot_position;
		err = cc_grammar_item_set_add_item(&ts, &ti);
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
	struct cc_grammar_item ti;
	struct cc_grammar_derivation td;
	const struct cc_grammar_item_base *bases[3];
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
		num_items[0] = valq_num_entries(&sets[0]->reduce_items);
		for (i = 0; i < num_items[0]; ++i) {
			items[0] = valq_peek_entry(&sets[0]->reduce_items, i);
			bases[0] = items[0]->base;
			sets[1] = valq_peek_entry(&this->item_sets, bases[0]->origin);
			assert(sets[1]);
			assert(sets[1]->index == bases[0]->origin);
			/*
			 * Find all items in origin set that contains a dot in front of
			 * the bases[0]->element. If found, add that item to set with dot
			 * moved one step.
			 */
			num_items[1] = valq_num_entries(&sets[1]->shift_items);
			for (j = 0; j < num_items[1]; ++j) {
				items[1] = valq_peek_entry(&sets[1]->shift_items, j);
				bases[1] = items[1]->base;
				ge = bases[1]->element;
				assert(ge);
				num_rules = valq_num_entries(&ge->rules);
				assert(num_rules);
				assert(bases[1]->rule < num_rules);
				gr = valq_peek_entry(&ge->rules, bases[1]->rule);
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
				if (ge != bases[0]->element)
					continue;
				ti = *items[1];
				++ti.dot_position;
				/*
				 * items[0] is responsible for moving the dot across
				 * items[1].element
				 */
				td.item_set = sets[0]->index;
				td.item = i;
				err = valq_add_tail(&ti.base->back, &td);
				if (err)
					return err;
				err = cc_grammar_item_set_add_item(set, &ti);
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
err_t compiler_run_prediction(struct compiler *this,
							  struct cc_grammar_item_set *set)
{
	int num_items_added, num_items, num_rhs, index, i, j, num_rules;
	const struct cc_grammar_item_base *base;
	const struct cc_grammar_item *item;
	struct cc_grammar_element *ge;
	const struct cc_grammar_rule *gr;
	const enum cc_token_type *ptype;
	enum cc_token_type type;
	struct cc_grammar_item_base *tb;
	struct cc_grammar_item ti;
	err_t err;
	bool set_changed;

	num_items_added = 0;
	do {
		set_changed = false;
		num_items = valq_num_entries(&set->shift_items);
		for (i = 0; i < num_items; ++i) {
			item = valq_peek_entry(&set->shift_items, i);
			base = item->base;
			assert(base);
			ge = base->element;
			assert(ge);
			num_rules = valq_num_entries(&ge->rules);
			assert(base->rule < num_rules);
			gr = valq_peek_entry(&ge->rules, base->rule);
			num_rhs = valq_num_entries(&gr->elements);
			assert(item->dot_position < num_rhs);	/* bcoz shift-item */
			ptype = valq_peek_entry(&gr->elements, item->dot_position);
			assert(ptype);
			type = *ptype;
			/* Prediction applicable only to non-terminals */
			if (!cc_token_type_is_non_terminal(type))
				continue;
			index = type - CC_TOKEN_TRANSLATION_OBJECT;
			ge = valq_peek_entry(&this->elements, index);
			assert(ge->type == type);
			num_rules = valq_num_entries(&ge->rules);
			for (j = 0; j < num_rules; ++j) {
				tb = malloc(sizeof(*tb));
				if (tb == NULL)
					return ENOMEM;
				ti.dot_position = 0;
				cc_grammar_item_base_init(tb, ge, j, set->index);
				err = cc_grammar_element_add_base_item(ge, tb, &ti.base);
				if (err)
					return err;
				assert(ti.base->element == ge);
				assert(ti.base->rule == j);
				assert(ti.base->origin == set->index);
				err = cc_grammar_item_set_add_item(set, &ti);
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

	err = EINVAL;
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
err_t compiler_compile(struct compiler *this)
{
	err_t err;
	struct cc_grammar_element *element;
	struct cc_grammar_item item;
	struct cc_grammar_item_base *tb;
	struct cc_grammar_item_set set;

	/* Prepare the item-set-#0 */
	tb = malloc(sizeof(*tb));
	if (tb == NULL)
		return ENOMEM;
	element = valq_peek_entry(&this->elements, 0);
	assert(element->type == CC_TOKEN_TRANSLATION_OBJECT);
	cc_grammar_item_base_init(tb, element, 0, 0);
	err = cc_grammar_element_add_base_item(element, tb, &item.base);
	if (err)
		return err;
	item.dot_position = 0;

	cc_grammar_item_set_init(&set, 0);
	err = cc_grammar_item_set_add_item(&set, &item);
	assert(err != EEXIST);
	if (!err)
		err = valq_add_tail(&this->item_sets, &set);
	if (err)
		return err;
	err = compiler_parse(this);
	printf("max_num_base_items = %d\n", max_num_base_items);
	return err;
}
