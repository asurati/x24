/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef INC_TYPES_H
#define INC_TYPES_H

#include <inc/errno.h>

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

#define NULL_CHAR	'\0'

/* Defined in main.c */
err_t	mkstemp(int *out_fd,
				const char **out_name);
char	*strdup(const char *str);

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))
/*****************************************************************************/
typedef void fn_array_entry_delete(void *);
struct array {
	void	**entries;
	int	num_entries;
	fn_array_entry_delete	*delete;
};

#define array_for_each(a, ix, entry)	\
	for (ix = 0;	\
		 entry = ix < array_num_entries(a) ? array_peek_entry(a, ix) : NULL,\
		 ix < array_num_entries(a);	\
		 ++ix)

static inline
void array_init(struct array *this,
				fn_array_entry_delete *delete)
{
	this->entries = NULL;
	this->num_entries = 0;
	this->delete = delete;
}

static inline
int array_num_entries(const struct array *this)
{
	return this->num_entries;
}

static inline
void *array_peek_entry(const struct array *this,
					   const int index)
{
	assert(0 <= index && index < this->num_entries);
	return this->entries[index];
}

static inline
void *array_remove_entry(struct array *this,
						 const int index)
{
	void *entry = array_peek_entry(this, index);
	assert(entry);
	this->entries[index] = NULL;
	return entry;
}

/* If delete fn isn't proivded, do not call this function */
static inline
void array_delete_entry(struct array *this,
						const int index)
{
	void *entry = array_remove_entry(this, index);
	this->delete(entry);
}

/* Do not delete 'this' */
static inline
void array_empty(struct array *this)
{
	int i;
	void *entry;

	array_for_each(this, i, entry) {
		if (entry == NULL)
			continue;
		array_delete_entry(this, i);
	}
	free(this->entries);
	array_init(this, this->delete);
}

/* Always adds at the tail */
err_t array_add_entry(struct array *this,
					  void *entry);
/*****************************************************************************/
typedef void fn_queue_entry_delete(void *);
struct queue {
	void	**entries;
	int	read;	/* points to the entry from which read should begin */
	int	num_entries;
	int	num_entries_allocated;
	fn_queue_entry_delete	*delete;
};

#define queue_for_each(q, ix, entry)	\
	for (ix = 0;	\
		 entry = ix < queue_num_entries(q) ? queue_peek_entry(q, ix) : NULL,\
		 ix < queue_num_entries(q);	\
		 ++ix)

#define queue_for_each_with_rem(q, entry)	\
	while (entry = queue_is_empty(q) ? NULL : queue_remove_head(q), entry)

#define queue_for_each_rev(q, ix, entry)	\
	for (ix = queue_num_entries(q) - 1;	\
		 entry = ix >= 0 ? queue_peek_entry(q, ix) : NULL, ix >= 0;	\
		 --ix)

#define queue_for_each_with_rem_rev(q, entry)	\
	while (entry = queue_is_empty(q) ? NULL : queue_remove_tail(q), entry)

static inline
void queue_init(struct queue *this,
				fn_queue_entry_delete *delete)
{
	this->entries = NULL;
	this->num_entries = 0;
	this->num_entries_allocated = 0;
	this->read = -1;	/* read == -1 implies empty. */
	this->delete = delete;
}

static inline
int queue_num_entries(const struct queue *this)
{
	return this->num_entries;
}

static inline
bool queue_is_full(const struct queue *this)
{
	return queue_num_entries(this) == this->num_entries_allocated;
}

static inline
bool queue_is_empty(const struct queue *this)
{
	return this->num_entries == 0;
}

static inline
void *queue_peek_entry(const struct queue *this,
					   const int index)
{
	int pos;

	assert(index >= 0 && index < this->num_entries);
	pos = (this->read + index) % this->num_entries_allocated;
	return this->entries[pos];
}

/* index 0 is the first entry in the queue */
static inline
void *queue_peek_head(const struct queue *this)
{
	return queue_peek_entry(this, 0);
}

/* index num_entries - 1 is the last entry in the queue */
static inline
void *queue_peek_tail(const struct queue *this)
{
	return queue_peek_entry(this, this->num_entries - 1);
}

/* Do not delete this. queue must be empty */
static inline
void queue_delete(struct queue *this)
{
	assert(queue_is_empty(this));
	free(this->entries);
	queue_init(this, this->delete);
}

static inline
void *queue_remove_head(struct queue *this)
{
	void *entry = queue_peek_head(this);
	this->read = (this->read + 1) % this->num_entries_allocated;
	--this->num_entries;
	if (queue_is_empty(this))
		queue_delete(this);
	return entry;
}

static inline
void *queue_remove_tail(struct queue *this)
{
	void *entry = queue_peek_tail(this);
	--this->num_entries;
	if (queue_is_empty(this))
		queue_delete(this);
	return entry;
}

static inline
void queue_delete_head(struct queue *this)
{
	void *entry = queue_remove_head(this);
	assert(entry);
	this->delete(entry);
	if (queue_is_empty(this))
		queue_delete(this);
}

static inline
void queue_delete_tail(struct queue *this)
{
	void *entry = queue_remove_tail(this);
	assert(entry);
	this->delete(entry);
	if (queue_is_empty(this))
		queue_delete(this);
}

static inline
void queue_empty(struct queue *this)
{
	while (!queue_is_empty(this))
		queue_delete_tail(this);
}

static inline
bool queue_find(const struct queue *this,
				const void *entry)
{
	int i;
	for (i = 0; i < queue_num_entries(this); ++i) {
		if (queue_peek_entry(this, i) == entry)
			return true;
	}
	return false;
}

err_t queue_add_head(struct queue *this,
					 void *entry);
err_t queue_add_tail(struct queue *this,
					 void *entry);
err_t queue_move(struct queue *this,
				 struct queue *out);
/*****************************************************************************/
/*
 * No need for a struct stack. But the callers must free the entries themselves,
 * as there's no delete func.
 */
static inline
void stack_init(struct queue *this)
{
	queue_init(this, NULL);
}

static inline
int stack_num_entries(const struct queue *this)
{
	return queue_num_entries(this);
}

static inline
void *stack_peek(const struct queue *this)
{
	return queue_peek_tail(this);
}

static inline
bool stack_is_empty(const struct queue *this)
{
	return queue_is_empty(this);
}

static inline
void *stack_pop(struct queue *this)
{
	assert(!stack_is_empty(this));
	return queue_remove_tail(this);
}

static inline
err_t stack_push(struct queue *this,
				 void *entry)
{
	return queue_add_tail(this, entry);
}

static inline
bool stack_find(struct queue *this,
				const void *entry)
{
	return queue_find(this, entry);
}
#endif
