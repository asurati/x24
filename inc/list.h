/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#ifndef INC_LIST_H
#define INC_LIST_H

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

struct list_entry {
	struct list_entry		*prev;
	struct list_entry		*next;
};

#ifndef container_of
#define container_of(p, t, m)	((t *)(void *)((char *)p - offsetof(t, m)))
#endif

#define list_entry(p, t, m)		container_of(p, t, m)
#define list_for_each(head, pos)	\
	for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)
#define list_for_each_rev(head, pos)	\
	for ((pos) = (head)->prev; (pos) != (head); (pos) = (pos)->prev)
#define list_for_each_with_del(head, pos)	\
	for ((pos) = list_del_head(head); (pos); (pos) = list_del_head(head))
#define list_for_each_rev_with_del(head, pos)	\
	for ((pos) = list_del_tail(head); (pos); (pos) = list_del_tail(head))

static inline
void list_init(struct list_entry *head)
{
	head->next = head->prev = head;
}

static inline
bool list_is_head(const struct list_entry *head,
				  const struct list_entry *e)
{
	return head->next == e;
}

static inline
bool list_is_tail(const struct list_entry *head,
				  const struct list_entry *e)
{
	return head->prev == e;
}

static inline
bool list_is_only(const struct list_entry *head,
				  const struct list_entry *e)
{
	return list_is_head(head, e) && list_is_tail(head, e);
}

static inline
bool list_is_empty(const struct list_entry *head)
{
	return list_is_head(head, head);
}

static inline
void list_add_between(struct list_entry *prev,
					  struct list_entry *next,
					  struct list_entry *n)
{
	n->next = next;
	n->prev = prev;
	next->prev = n;
	prev->next = n;
}

static inline
void list_add_head(struct list_entry *head,
				   struct list_entry *n)
{
	list_add_between(head, head->next, n);
}

static inline
void list_add_tail(struct list_entry *head,
				   struct list_entry *n)
{
	list_add_between(head->prev, head, n);
}

static inline
void list_del_entry(struct list_entry *entry)
{
	struct list_entry *prev, *next;

	next = entry->next;
	prev = entry->prev;
	next->prev = prev;
	prev->next = next;
}

static inline
struct list_entry *list_del_tail(struct list_entry *head)
{
	struct list_entry *entry = head->prev;

	if (list_is_empty(head))
		return NULL;

	list_del_entry(entry);
	return entry;
}

static inline
struct list_entry *list_del_head(struct list_entry *head)
{
	struct list_entry *entry = head->next;

	if (list_is_empty(head))
		return NULL;

	list_del_entry(entry);
	return entry;
}

static inline
struct list_entry *list_peek_tail(const struct list_entry *head)
{
	return head->prev;
}

static inline
struct list_entry *list_peek_head(const struct list_entry *head)
{
	return head->next;
}

static inline
void list_move(struct list_entry *from,
			   struct list_entry *to)
{
	struct list_entry *e;

	list_for_each_with_del(from, e)
		list_add_tail(to, e);
}
#endif
