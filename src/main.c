/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include <inc/cpp/scanner.h>
#include <inc/cc/parser.h>
#include <inc/types.h>

#include <assert.h>
#include <stdio.h>
#include <locale.h>
/*****************************************************************************/
int main(int argc, char **argv)
{
	err_t err;
	const char *path;
	struct scanner *scanner;
	struct parser *parser;

	if (argc != 2) {
		printf("Usage: %s path.to.src.c\n", argv[0]);
		return EINVAL;
	}

	setlocale(LC_ALL, "en_US.utf8");
	err = scanner_new(&scanner);
	if (err)
		goto err0;
	err = scanner_scan(scanner, argv[1]);
	if (err)
		goto err1;
	path = scanner_cpp_tokens_path(scanner);	/* path owned by scanner */
	assert(path);
	path = strdup(path);
	if (path == NULL) {
		err = ENOMEM;
		goto err1;
	}
	scanner_delete(scanner);
	scanner = NULL;
	err = parser_new(path, &parser);	/* path owned by parser */
	if (err)
		goto err1;
	err = parser_parse(parser);
	goto err2;
err2:
	if (parser)
		parser_delete(parser);
err1:
	if (scanner)
		scanner_delete(scanner);
err0:
	setlocale(LC_ALL, "C");
	return err;
}
