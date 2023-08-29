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
#include <string.h>

#define NULL_CHAR	'\0'

/* Defined in main.c */
err_t	mkstemp(int *out_fd,
				const char **out_name);
char	*strdup(const char *str);

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))
/*****************************************************************************/
#define QUEUE_FOR_EACH(q, ix, e)	\
	for (ix = 0; ix < queue_num_entries(q) ?	\
		 e = queue_peek_entry(q, ix) : NULL; ++ix)

#define QUEUE_FOR_EACH_REVERSE(name, q, ix, e)	\
	for (ix = queue_num_entries( - 1;	\
		 e = ix >= 0 ? name ## _queue_peek_entry(q, ix) : e, ix >= 0; --ix)

#define QUEUE_FOR_EACH_WITH_REMOVE_REVERSE(name, q, c, e)	\
	while (c = name ## _queue_is_empty(q),	\
		   e = c ? e : name ## _queue_remove_tail(q), \
		   !c)

/* Queue is implemented without holes. Allows removal from the middle. */
/*
 * Deleteion recieves a ptr to the entry, which the delete routine must not
 * free.
 */
typedef void fn_queue_delete_entry(void *p);
struct queue {
	int	num_entries;
	int	num_entries_allocated;
	int	entry_size;
	int	read;
	char	*entries;
	fn_queue_delete_entry	*delete;
};

void	queue_remove_entry(struct queue *this,
						   const int index);
err_t	queue_add_head(struct queue *this,
					   const void *entry);
err_t	queue_add_tail(struct queue *this,
					   const void *entry);
err_t	queue_move(struct queue *this,
				   struct queue *to);

static inline
void queue_init(struct queue *this,
				const int entry_size,
				fn_queue_delete_entry *delete)
{
	this->num_entries_allocated = this->num_entries = this->read = 0;
	this->entry_size = entry_size;
	this->delete = delete;
	this->entries = NULL;
}

static inline
bool queue_is_empty(const struct queue *this)
{
	return this->num_entries == 0;
}

static inline
bool queue_is_full(const struct queue *this)
{
	return this->num_entries == this->num_entries_allocated;
}

static inline
int queue_num_entries(const struct queue *this)
{
	return this->num_entries;
}

/*
 * Always returns the pointer to the entry. The address is within the entries
 * array. The address can change; if the caller wants the entry to remain at a
 * fixed address, they can instead create the queue for &entry.
 */
static inline
void *queue_peek_entry(const struct queue *this,
					   const int index)
{
	int pos;
	assert(0 <= index && index < this->num_entries);
	pos = (this->read + index) % this->num_entries_allocated;
	return this->entries + pos * this->entry_size;
}

/*
 * Note that we use memcmp; all queue entries must be initialize with memset so
 * that any compiler-generated padding is set to 0.
 */
static inline
bool queue_find(const struct queue *this,
				const void *entry)
{
	int i;
	for (i = 0; i < this->num_entries; ++i) {
		if (!memcmp(queue_peek_entry(this, i), entry, this->entry_size))
			return true;
	}
	return false;
}

static inline
void queue_delete_entry(struct queue *this,
						const int index)
{
	this->delete(queue_peek_entry(this, index));
	queue_remove_entry(this, index);
}

static inline
void *queue_peek_head(struct queue *this)
{
	assert(!queue_is_empty(this));
	return queue_peek_entry(this, 0);
}

static inline
void *queue_peek_tail(struct queue *this)
{
	assert(!queue_is_empty(this));
	return queue_peek_entry(this, this->num_entries - 1);
}

static inline
void queue_remove_head(struct queue *this)
{
	assert(!queue_is_empty(this));
	queue_remove_entry(this, 0);
}

static inline
void queue_remove_tail(struct queue *this)
{
	assert(!queue_is_empty(this));
	queue_remove_entry(this, this->num_entries - 1);
}

static inline
void queue_delete_head(struct queue *this)
{
	assert(!queue_is_empty(this));
	queue_delete_entry(this, 0);
}

static inline
void queue_delete_tail(struct queue *this)
{
	assert(!queue_is_empty(this));
	queue_delete_entry(this, this->num_entries - 1);
}

static inline
void queue_empty(struct queue *this)
{
	while (!queue_is_empty(this))
		queue_delete_head(this);
}
/*****************************************************************************/
#endif
