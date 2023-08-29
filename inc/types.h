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
/*
 * Queue is implemented without holes. An item removed will cause the
 * subsequent items to shift forward. This changes the address of the items
 * that are moved. Deletion provides type * which the del routine should not
 * free.
 */
#define QUEUE_FOR_EACH(name, q, ix, e)	\
	for (ix = 0;	e = ix < name ## _queue_num_entries(q) ?	\
		 name ## _queue_peek_entry(q, ix) : e,	\
		 ix < name ## _queue_num_entries(q); ++ix)

#define QUEUE_FOR_EACH_WITH_REMOVE(name, q, c, e)	\
	while (c = name ## _queue_is_empty(q),	\
		   e = c ? e : name ## _queue_remove_head(q), \
		   !c)

#define QUEUE_FOR_EACH_REVERSE(name, q, ix, e)	\
	for (ix = name ## _queue_num_entries - 1;	\
		 e = ix >= 0 ? name ## _queue_peek_entry(q, ix) : e, ix >= 0; --ix)

#define QUEUE_FOR_EACH_WITH_REMOVE_REVERSE(name, q, c, e)	\
	while (c = name ## _queue_is_empty(q),	\
		   e = c ? e : name ## _queue_remove_tail(q), \
		   !c)

#define DEFINE_QUEUE_TYPE(name, type)	\
typedef void fn_ ## name ## _queue_delete_entry(type entry);	\
struct name ## _queue {	\
	type	*entries;	\
	int		num_entries, num_entries_allocated, read, size;	\
	fn_ ## name ## _queue_delete_entry	*delete;	\
};	\
static inline	\
bool name ## _queue_is_empty(const struct name ## _queue *this) {	\
	return this->num_entries == 0;}	\
static inline	\
bool name ## _queue_is_full(const struct name ## _queue *this) {	\
	return this->num_entries == this->num_entries_allocated;}	\
static inline	\
int name ## _queue_num_entries(struct name ## _queue *this) {	\
	return this->num_entries; }	\
static inline	\
void name ## _queue_init(struct name ## _queue *this,	\
						 fn_ ## name ## _queue_delete_entry *delete) {	\
	this->entries = NULL; this->delete = delete; this->read = -1; \
	this->num_entries_allocated = this->num_entries = 0;	\
	this->size = sizeof(type);}	\
static inline	\
type *name ## _queue_peek_entry_address(struct name ## _queue *this,	\
										const int index)	{int pos;	\
	assert(0 <= index && index < this->num_entries);	\
	pos = (this->read + index) % this->num_entries_allocated;	\
	return &this->entries[pos];}	\
static inline	\
type name ## _queue_peek_entry(struct name ## _queue *this,	\
							   const int index)	{	\
	return *name ## _queue_peek_entry_address(this, index);}	\
static inline	\
bool name ## _queue_find(struct name ## _queue *this,	\
						 const type entry) {int i;	\
	for (i = 0; i < this->num_entries; ++i)	\
	if (name ## _queue_peek_entry(this, i) == entry) return true;	\
	return false;}	\
void name ## _queue_delete_entry(struct name ## _queue *this,	\
								 const int index) {	\
	this->delete(name ## _queue_peek_entry(this, index));	\
	name ## _queue_remove_entry(this, index);}	\
static inline	\
type name ## _queue_peek_head(struct name ## _queue *this) {	\
	return name ## _queue_peek_entry(this, 0);}	\
static inline	\
type name ## _queue_peek_tail(struct name ## _queue *this) {	\
	return name ## _queue_peek_entry(this, this->num_entries - 1);}	\
static inline	\
type *name ## _queue_peek_head_address(struct name ## _queue *this) {	\
	return name ## _queue_peek_entry_address(this, 0);}	\
static inline	\
type *name ## _queue_peek_tail_address(struct name ## _queue *this) {	\
	return name ## _queue_peek_entry_address(this, this->num_entries - 1);}	\
static inline	\
type name ## _queue_remove_head(struct name ## _queue *this) {	\
	return name ## _queue_remove_entry(this, 0);}	\
static inline	\
type name ## _queue_remove_tail(struct name ## _queue *this) {	\
	return name ## _queue_remove_entry(this, this->num_entries - 1);}	\
static inline	\
void name ## _queue_delete_head(struct name ## _queue *this) {	\
	name ## _queue_delete_entry(this, 0);}	\
static inline	\
void name ## _queue_delete_tail(struct name ## _queue *this) {	\
	name ## _queue_delete_entry(this, this->num_entries - 1); }	\
void name ## _queue_empty(struct name ## _queue *this) {	\
	while (!name ## _queue_is_empty(this))	\
	name ## _queue_delete_head(this); }	\
err_t queue_move(struct name ## _queue *this, struct name ## _queue *to) {	\
	while (!name ## _queue_is_empty(this))	{err_t err;	type entry; \
		entry = name ## _queue_remove_head(this);	\
		err = name ## _queue_add_tail(to, entry);	if (err) return err;} \
err_t name ## _queue_add_entry(struct name ## _queue *this,	\
							   type entry);\
type name ## _queue_remove_entry(struct name ## _queue *this,	\
								 const int index);
#endif
