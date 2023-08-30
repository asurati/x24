/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */
/* vim: set noet ts=4 sts=4 sw=4: */

#include <inc/cpp/scanner.h>
#include <inc/cc/compiler.h>
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
	//struct compiler *compiler;

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
	path = scanner_cpp_tokens_path(scanner);
	assert(path);
	path = strdup(path);
	if (path == NULL) {
		err = ENOMEM;
		goto err1;
	}
	scanner_delete(scanner);
	scanner = NULL;
#if 0
	err = compiler_new(path, &compiler);	/* path owned by compiler */
	if (err)
		goto err1;
	err = compiler_compile(compiler);
	goto err2;
err2:
	if (compiler)
		compiler_delete(compiler);
#endif
err1:
	if (scanner)
		scanner_delete(scanner);
err0:
	setlocale(LC_ALL, "C");
	return err;
}
