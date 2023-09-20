// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2023 Amol Surati
// vim: set noet ts=4 sts=4 sw=4:

// cc -O3 -Wall -Wextra -I.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define NODE(t)
enum cc_token_type {
#define DEF(t)	CC_TOKEN_ ## t,
#include <inc/cpp/tokens.h>
#include <inc/cc/tokens.h>
#undef DEF
};

const char *cc_token_type_strs[] = {
#define DEF(t)	"CC_TOKEN_" # t,
#include <inc/cpp/tokens.h>
#include <inc/cc/tokens.h>
#undef DEF
};
#undef NODE

/* In order of appearance from A.2.1 Expressions */
const char *terminals[] = {
	"(",
	")",
	"_Generic",
	",",
	":",
	"default",
	"[",
	"]",
	".",
	"->",
	"++",
	"--",
	"sizeof",
	"alignof",
	"&",
	"*",
	"+",
	"-",
	"~",
	"!",
	"/",
	"%",
	"<<",
	">>",
	"<",
	">",
	"<=",
	">=",
	"==",
	"!=",
	"^",
	"|",
	"&&",
	"||",
	"?",
	"=",
	"*=",
	"/=",
	"%=",
	"+=",
	"-=",
	"<<=",
	">>=",
	"&=",
	"^=",
	"|=",
	";",
	"auto",
	"constexpr",
	"extern",
	"register",
	"static",
	"thread_local",
	"typedef",
	"void",
	"char",
	"short",
	"int",
	"long",
	"float",
	"double",
	"signed",
	"unsigned",
	"_BitInt",
	"bool",
	"_Complex",
	"_Decimal32",
	"_Decimal64",
	"_Decimal128",
	"{",
	"}",
	"struct",
	"union",
	"enum",
	"_Atomic",
	"typeof",
	"typeof_unqual",
	"const",
	"restrict",
	"volatile",
	"inline",
	"noreturn",	/* _Noreturn is obsolete */
	"alignas",
	"static",
	"...",
	"static_assert",
	"::",
	"case",
	"if",
	"switch",
	"else",
	"while",
	"do",
	"for",
	"goto",
	"continue",
	"break",
	"return",
	"Identifier",
	"true",
	"false",
	"nullptr",
	"IntegerConstant",
	"FloatingConstant",
	"IntegerCharConstant",
	"Utf8CharConstant",
	"Utf16CharConstant",
	"Utf32CharConstant",
	"WcharTCharConstant",
	"CharStringLiteral",
	"Utf8StringLiteral",
	"Utf16StringLiteral",
	"Utf32StringLiteral",
	"WcharTStringLiteral",
};

#define DEF(t) CC_TOKEN_ ## t
enum cc_token_type name_to_type(const char *name)
{
	if (!strcmp(name,"(")) return DEF(LEFT_PAREN);
	if (!strcmp(name,")")) return DEF(RIGHT_PAREN);
	if (!strcmp(name,"_Generic")) return DEF(GENERIC);
	if (!strcmp(name,",")) return DEF(COMMA);
	if (!strcmp(name,":")) return DEF(COLON);
	if (!strcmp(name,"default")) return DEF(DEFAULT);
	if (!strcmp(name,"[")) return DEF(LEFT_BRACKET);
	if (!strcmp(name,"]")) return DEF(RIGHT_BRACKET);
	if (!strcmp(name,".")) return DEF(DOT);
	if (!strcmp(name,"->")) return DEF(ARROW);
	if (!strcmp(name,"++")) return DEF(INCR);
	if (!strcmp(name,"--")) return DEF(DECR);
	if (!strcmp(name,"sizeof")) return DEF(SIZE_OF);
	if (!strcmp(name,"alignof")) return DEF(ALIGN_OF);
	if (!strcmp(name,"&")) return DEF(BITWISE_AND);
	if (!strcmp(name,"*")) return DEF(MUL);
	if (!strcmp(name,"+")) return DEF(PLUS);
	if (!strcmp(name,"-")) return DEF(MINUS);
	if (!strcmp(name,"~")) return DEF(BITWISE_NOT);
	if (!strcmp(name,"!")) return DEF(LOGICAL_NOT);
	if (!strcmp(name,"/")) return DEF(DIV);
	if (!strcmp(name,"%")) return DEF(MOD);
	if (!strcmp(name,"<<")) return DEF(SHIFT_LEFT);
	if (!strcmp(name,">>")) return DEF(SHIFT_RIGHT);
	if (!strcmp(name,"<")) return DEF(LESS_THAN);
	if (!strcmp(name,">")) return DEF(GREATER_THAN);
	if (!strcmp(name,"<=")) return DEF(LESS_THAN_EQUALS);
	if (!strcmp(name,">=")) return DEF(GREATER_THAN_EQUALS);
	if (!strcmp(name,"==")) return DEF(EQUALS);
	if (!strcmp(name,"!=")) return DEF(NOT_EQUALS);
	if (!strcmp(name,"^")) return DEF(BITWISE_XOR);
	if (!strcmp(name,"|")) return DEF(BITWISE_OR);
	if (!strcmp(name,"&&")) return DEF(LOGICAL_AND);
	if (!strcmp(name,"||")) return DEF(LOGICAL_OR);
	if (!strcmp(name,"?")) return DEF(CONDITIONAL);
	if (!strcmp(name,"=")) return DEF(ASSIGN);
	if (!strcmp(name,"*=")) return DEF(MUL_ASSIGN);
	if (!strcmp(name,"/=")) return DEF(DIV_ASSIGN);
	if (!strcmp(name,"%=")) return DEF(MOD_ASSIGN);
	if (!strcmp(name,"+=")) return DEF(PLUS_ASSIGN);
	if (!strcmp(name,"-=")) return DEF(MINUS_ASSIGN);
	if (!strcmp(name,"<<=")) return DEF(SHIFT_LEFT_ASSIGN);
	if (!strcmp(name,">>=")) return DEF(SHIFT_RIGHT_ASSIGN);
	if (!strcmp(name,"&=")) return DEF(BITWISE_AND_ASSIGN);
	if (!strcmp(name,"^=")) return DEF(BITWISE_XOR_ASSIGN);
	if (!strcmp(name,"|=")) return DEF(BITWISE_OR_ASSIGN);
	if (!strcmp(name,";")) return DEF(SEMI_COLON);
	if (!strcmp(name,"auto")) return DEF(AUTO);
	if (!strcmp(name,"constexpr")) return DEF(CONST_EXPR);
	if (!strcmp(name,"extern")) return DEF(EXTERN);
	if (!strcmp(name,"register")) return DEF(REGISTER);
	if (!strcmp(name,"static")) return DEF(STATIC);
	if (!strcmp(name,"thread_local")) return DEF(THREAD_LOCAL);
	if (!strcmp(name,"typedef")) return DEF(TYPE_DEF);
	if (!strcmp(name,"void")) return DEF(VOID);
	if (!strcmp(name,"char")) return DEF(CHAR);
	if (!strcmp(name,"short")) return DEF(SHORT);
	if (!strcmp(name,"int")) return DEF(INT);
	if (!strcmp(name,"long")) return DEF(LONG);
	if (!strcmp(name,"float")) return DEF(FLOAT);
	if (!strcmp(name,"double")) return DEF(DOUBLE);
	if (!strcmp(name,"signed")) return DEF(SIGNED);
	if (!strcmp(name,"unsigned")) return DEF(UNSIGNED);
	if (!strcmp(name,"_BitInt")) return DEF(BIT_INT);
	if (!strcmp(name,"bool")) return DEF(BOOL);
	if (!strcmp(name,"_Complex")) return DEF(COMPLEX);
	if (!strcmp(name,"_Decimal32")) return DEF(DECIMAL_32);
	if (!strcmp(name,"_Decimal64")) return DEF(DECIMAL_64);
	if (!strcmp(name,"_Decimal128")) return DEF(DECIMAL_128);
	if (!strcmp(name,"{")) return DEF(LEFT_BRACE);
	if (!strcmp(name,"}")) return DEF(RIGHT_BRACE);
	if (!strcmp(name,"struct")) return DEF(STRUCT);
	if (!strcmp(name,"union")) return DEF(UNION);
	if (!strcmp(name,"enum")) return DEF(ENUM);
	if (!strcmp(name,"_Atomic")) return DEF(ATOMIC);
	if (!strcmp(name,"typeof")) return DEF(TYPE_OF);
	if (!strcmp(name,"typeof_unqual")) return DEF(TYPE_OF_UNQUAL);
	if (!strcmp(name,"const")) return DEF(CONST);
	if (!strcmp(name,"restrict")) return DEF(RESTRICT);
	if (!strcmp(name,"volatile")) return DEF(VOLATILE);
	if (!strcmp(name,"inline")) return DEF(INLINE);
	if (!strcmp(name,"noreturn")) return DEF(NO_RETURN);
	if (!strcmp(name,"alignas")) return DEF(ALIGN_AS);
	if (!strcmp(name,"static")) return DEF(STATIC);
	if (!strcmp(name,"...")) return DEF(ELLIPSIS);
	if (!strcmp(name,"static_assert")) return DEF(STATIC_ASSERT);
	if (!strcmp(name,"::")) return DEF(DOUBLE_COLON);
	if (!strcmp(name,"case")) return DEF(CASE);
	if (!strcmp(name,"if")) return DEF(IF);
	if (!strcmp(name,"switch")) return DEF(SWITCH);
	if (!strcmp(name,"else")) return DEF(ELSE);
	if (!strcmp(name,"while")) return DEF(WHILE);
	if (!strcmp(name,"do")) return DEF(DO);
	if (!strcmp(name,"for")) return DEF(FOR);
	if (!strcmp(name,"goto")) return DEF(GO_TO);
	if (!strcmp(name,"continue")) return DEF(CONTINUE);
	if (!strcmp(name,"break")) return DEF(BREAK);
	if (!strcmp(name,"return")) return DEF(RETURN);
	if (!strcmp(name,"Identifier")) return DEF(IDENTIFIER);
	if (!strcmp(name,"true")) return DEF(TRUE);
	if (!strcmp(name,"false")) return DEF(FALSE);
	if (!strcmp(name,"nullptr")) return DEF(NULL_PTR);
	if (!strcmp(name,"IntegerConstant")) return DEF(INTEGER_CONST);
	if (!strcmp(name,"FloatingConstant")) return DEF(FLOATING_CONST);
	if (!strcmp(name,"IntegerCharConstant")) return DEF(INTEGER_CHAR_CONST);
	if (!strcmp(name,"Utf8CharConstant")) return DEF(UTF_8_CHAR_CONST);
	if (!strcmp(name,"Utf16CharConstant")) return DEF(UTF_16_CHAR_CONST);
	if (!strcmp(name,"Utf32CharConstant")) return DEF(UTF_32_CHAR_CONST);
	if (!strcmp(name,"WcharTCharConstant")) return DEF(WCHAR_T_CHAR_CONST);
	if (!strcmp(name,"CharStringLiteral")) return DEF(CHAR_STRING_LITERAL);
	if (!strcmp(name,"Utf8StringLiteral")) return DEF(UTF_8_STRING_LITERAL);
	if (!strcmp(name,"Utf16StringLiteral")) return DEF(UTF_16_STRING_LITERAL);
	if (!strcmp(name,"Utf32StringLiteral")) return DEF(UTF_32_STRING_LITERAL);
	if (!strcmp(name,"WcharTStringLiteral"))return DEF(WCHAR_T_STRING_LITERAL);

	if (!strcmp(name,"AbstractDeclarator")) return DEF(ABSTRACT_DECLARATOR);
	if (!strcmp(name,"AdditiveExpression")) return DEF(ADDITIVE_EXPRESSION);
	if (!strcmp(name,"AlignmentSpecifier")) return DEF(ALIGNMENT_SPECIFIER);
	if (!strcmp(name,"AndExpression")) return DEF(AND_EXPRESSION);
	if (!strcmp(name,"ArgumentExpressionList")) return DEF(ARGUMENT_EXPRESSION_LIST);
	if (!strcmp(name,"ArrayAbstractDeclarator")) return DEF(ARRAY_ABSTRACT_DECLARATOR);
	if (!strcmp(name,"ArrayDeclarator")) return DEF(ARRAY_DECLARATOR);
	if (!strcmp(name,"AssignmentExpression")) return DEF(ASSIGNMENT_EXPRESSION);
	if (!strcmp(name,"AssignmentOperator")) return DEF(ASSIGNMENT_OPERATOR);
	if (!strcmp(name,"AtomicTypeSpecifier")) return DEF(ATOMIC_TYPE_SPECIFIER);
	if (!strcmp(name,"Attribute")) return DEF(ATTRIBUTE);
	if (!strcmp(name,"AttributeArgumentClause")) return DEF(ATTRIBUTE_ARGUMENT_CLAUSE);
	if (!strcmp(name,"AttributeDeclaration")) return DEF(ATTRIBUTE_DECLARATION);
	if (!strcmp(name,"AttributeList")) return DEF(ATTRIBUTE_LIST);
	if (!strcmp(name,"AttributePrefix")) return DEF(ATTRIBUTE_PREFIX);
	if (!strcmp(name,"AttributePrefixedToken")) return DEF(ATTRIBUTE_PREFIXED_TOKEN);
	if (!strcmp(name,"AttributeSpecifier")) return DEF(ATTRIBUTE_SPECIFIER);
	if (!strcmp(name,"AttributeSpecifierSequence")) return DEF(ATTRIBUTE_SPECIFIER_SEQUENCE);
	if (!strcmp(name,"AttributeToken")) return DEF(ATTRIBUTE_TOKEN);
	if (!strcmp(name,"BalancedToken")) return DEF(BALANCED_TOKEN);
	if (!strcmp(name,"BalancedTokenSequence")) return DEF(BALANCED_TOKEN_SEQUENCE);
	if (!strcmp(name,"BlockItem")) return DEF(BLOCK_ITEM);
	if (!strcmp(name,"BlockItemList")) return DEF(BLOCK_ITEM_LIST);
	if (!strcmp(name,"BracedInitializer")) return DEF(BRACED_INITIALIZER);
	if (!strcmp(name,"CastExpression")) return DEF(CAST_EXPRESSION);
	if (!strcmp(name,"CompoundLiteral")) return DEF(COMPOUND_LITERAL);
	if (!strcmp(name,"CompoundStatement")) return DEF(COMPOUND_STATEMENT);
	if (!strcmp(name,"ConditionalExpression")) return DEF(CONDITIONAL_EXPRESSION);
	if (!strcmp(name,"ConstantExpression")) return DEF(CONSTANT_EXPRESSION);
	if (!strcmp(name,"Declaration")) return DEF(DECLARATION);
	if (!strcmp(name,"DeclarationSpecifier")) return DEF(DECLARATION_SPECIFIER);
	if (!strcmp(name,"DeclarationSpecifiers")) return DEF(DECLARATION_SPECIFIERS);
	if (!strcmp(name,"Declarator")) return DEF(DECLARATOR);
	if (!strcmp(name,"Designation")) return DEF(DESIGNATION);
	if (!strcmp(name,"Designator")) return DEF(DESIGNATOR);
	if (!strcmp(name,"DesignatorList")) return DEF(DESIGNATOR_LIST);
	if (!strcmp(name,"DirectAbstractDeclarator")) return DEF(DIRECT_ABSTRACT_DECLARATOR);
	if (!strcmp(name,"DirectDeclarator")) return DEF(DIRECT_DECLARATOR);
	if (!strcmp(name,"EnumerationConstant")) return DEF(ENUMERATION_CONSTANT);
	if (!strcmp(name,"Enumerator")) return DEF(ENUMERATOR);
	if (!strcmp(name,"EnumeratorList")) return DEF(ENUMERATOR_LIST);
	if (!strcmp(name,"EnumSpecifier")) return DEF(ENUM_SPECIFIER);
	if (!strcmp(name,"EnumTypeSpecifier")) return DEF(ENUM_TYPE_SPECIFIER);
	if (!strcmp(name,"EqualityExpression")) return DEF(EQUALITY_EXPRESSION);
	if (!strcmp(name,"ExclusiveOrExpression")) return DEF(EXLUSIVE_OR_EXPRESSION);
	if (!strcmp(name,"Expression")) return DEF(EXPRESSION);
	if (!strcmp(name,"ExpressionStatement")) return DEF(EXPRESSION_STATEMENT);
	if (!strcmp(name,"ExternalDeclaration")) return DEF(EXTERNAL_DECLARATION);
	if (!strcmp(name,"FunctionAbstractDeclarator")) return DEF(FUNCTION_ABSTRACT_DECLARATOR);
	if (!strcmp(name,"FunctionBody")) return DEF(FUNCTION_BODY);
	if (!strcmp(name,"FunctionDeclarator")) return DEF(FUNCTION_DECLARATOR);
	if (!strcmp(name,"FunctionDefinition")) return DEF(FUNCTION_DEFINITION);
	if (!strcmp(name,"FunctionSpecifier")) return DEF(FUNCTION_SPECIFIER);
	if (!strcmp(name,"GenericAssociation")) return DEF(GENERIC_ASSOCIATION);
	if (!strcmp(name,"GenericAssocList")) return DEF(GENERIC_ASSOC_LIST);
	if (!strcmp(name,"GenericSelection")) return DEF(GENERIC_SELECTION);
	if (!strcmp(name,"InclusiveOrExpression")) return DEF(INCLUSIVE_OR_EXPRESSION);
	if (!strcmp(name,"InitDeclarator")) return DEF(INIT_DECLARATOR);
	if (!strcmp(name,"InitDeclaratorList")) return DEF(INIT_DECLARATOR_LIST);
	if (!strcmp(name,"Initializer")) return DEF(INITIALIZER);
	if (!strcmp(name,"InitializerList")) return DEF(INITIALIZER_LIST);
	if (!strcmp(name,"IterationStatement")) return DEF(ITERATION_STATEMENT);
	if (!strcmp(name,"JumpStatement")) return DEF(JUMP_STATEMENT);
	if (!strcmp(name,"Label")) return DEF(LABEL);
	if (!strcmp(name,"LabeledStatement")) return DEF(LABELED_STATEMENT);
	if (!strcmp(name,"LogicalAndExpression")) return DEF(LOGICAL_AND_EXPRESSION);
	if (!strcmp(name,"LogicalOrExpression")) return DEF(LOGICAL_OR_EXPRESSION);
	if (!strcmp(name,"MemberDeclaration")) return DEF(MEMBER_DECLARATION);
	if (!strcmp(name,"MemberDeclarationList")) return DEF(MEMBER_DECLARATION_LIST);
	if (!strcmp(name,"MemberDeclarator")) return DEF(MEMBER_DECLARATOR);
	if (!strcmp(name,"MemberDeclaratorList")) return DEF(MEMBER_DECLARATOR_LIST);
	if (!strcmp(name,"MultiplicativeExpression")) return DEF(MULTIPLICATIVE_EXPRESSION);
	if (!strcmp(name,"ParameterDeclaration")) return DEF(PARAMETER_DECLARATION);
	if (!strcmp(name,"ParameterList")) return DEF(PARAMETER_LIST);
	if (!strcmp(name,"ParameterTypeList")) return DEF(PARAMETER_TYPE_LIST);
	if (!strcmp(name,"Pointer")) return DEF(POINTER);
	if (!strcmp(name,"PostfixExpression")) return DEF(POSTFIX_EXPRESSION);
	if (!strcmp(name,"PrimaryBlock")) return DEF(PRIMARY_BLOCK);
	if (!strcmp(name,"PrimaryExpression")) return DEF(PRIMARY_EXPRESSION);
	if (!strcmp(name,"RelationalExpression")) return DEF(RELATIONAL_EXPRESSION);
	if (!strcmp(name,"SecondaryBlock")) return DEF(SECONDARY_BLOCK);
	if (!strcmp(name,"SelectionStatement")) return DEF(SELECTION_STATEMENT);
	if (!strcmp(name,"ShiftExpression")) return DEF(SHIFT_EXPRESSION);
	if (!strcmp(name,"SpecifierQualifierList")) return DEF(SPECIFIER_QUALIFIER_LIST);
	if (!strcmp(name,"StandardAttribute")) return DEF(STANDARD_ATTRIBUTE);
	if (!strcmp(name,"Statement")) return DEF(STATEMENT);
	if (!strcmp(name,"StaticAssertDeclaration")) return DEF(STATIC_ASSERT_DECLARATION);
	if (!strcmp(name,"StorageClassSpecifier")) return DEF(STORAGE_CLASS_SPECIFIER);
	if (!strcmp(name,"StorageClassSpecifiers")) return DEF(STORAGE_CLASS_SPECIFIERS);
	if (!strcmp(name,"StructOrUnion")) return DEF(STRUCT_OR_UNION);
	if (!strcmp(name,"StructOrUnionSpecifier")) return DEF(STRUCT_OR_UNION_SPECIFIER);
	if (!strcmp(name,"TranslationObject")) return DEF(TRANSLATION_OBJECT);
	if (!strcmp(name,"TranslationUnit")) return DEF(TRANSLATION_UNIT);
	if (!strcmp(name,"TypedefName")) return DEF(TYPE_DEF_NAME);
	if (!strcmp(name,"TypeName")) return DEF(TYPE_NAME);
	if (!strcmp(name,"TypeofSpecifier")) return DEF(TYPE_OF_SPECIFIER);
	if (!strcmp(name,"TypeofSpecifierArgument")) return DEF(TYPE_OF_SPECIFIER_ARGUMENT);
	if (!strcmp(name,"TypeQualifier")) return DEF(TYPE_QUALIFIER);
	if (!strcmp(name,"TypeQualifierList")) return DEF(TYPE_QUALIFIER_LIST);
	if (!strcmp(name,"TypeSpecifier")) return DEF(TYPE_SPECIFIER);
	if (!strcmp(name,"TypeSpecifierQualifier")) return DEF(TYPE_SPECIFIER_QUALIFIER);
	if (!strcmp(name,"UnaryExpression")) return DEF(UNARY_EXPRESSION);
	if (!strcmp(name,"UnaryOperator")) return DEF(UNARY_OPERATOR);
	if (!strcmp(name,"UnlabeledStatement")) return DEF(UNLABELED_STATEMENT);
	assert(0);
}
///////////////////////////////////////////////////////////////////////////////
struct rule {
	enum cc_token_type	*rhs;
	int	num_rhs;
};

struct element {
	enum cc_token_type type;
	struct rule	*rules;
	int	num_rules;
};

struct element *elements;
int num_elements;
///////////////////////////////////////////////////////////////////////////////
/* Terminals: Identifier, StringLiteral, Constant, epsilon */
static
bool is_terminal(const enum cc_token_type type)
{
	return type < CC_TOKEN_TRANSLATION_OBJECT;	/* See inc/cc/tokens.h */
}

static
void print_rule(const struct rule *r, const int lhs)
{
	int i, j;
	const struct element *e;

	e = &elements[lhs];
	printf("%s:", cc_token_type_strs[e->type]);
	for (i = 0; i < r->num_rhs; ++i) {
		j = r->rhs[i];
		e = &elements[j];
		printf(" %s", cc_token_type_strs[e->type]);
	}
	printf("\n");
}

static
void print_element(const int index)
{
	int i;
	const struct element *e;

	e = &elements[index];
	if (is_terminal(e->type))
		return;

	for (i = 0; i < e->num_rules; ++i)
		print_rule(&e->rules[i], index);
}

static
int find_element(const enum cc_token_type type)
{
	int i;

	for (i = 0; i < num_elements; ++i)
		if (elements[i].type == type)
			return i;
	return -1;
}

int add_element(const char *name)
{
	int i;
	struct element *e;
	enum cc_token_type type;

	type = name_to_type(name);
	i = find_element(type);
	if (i >= 0)
		return i;

	/* not found */
	elements = realloc(elements, (num_elements + 1) * sizeof(*e));
	e = &elements[num_elements++];
	memset(e, 0, sizeof(*e));
	e->type = type;
	return num_elements - 1;
}

static
void cleanup()
{
	int i, j;
	struct element *e;
	struct rule *r;

	for (i = 0; i < num_elements; ++i) {
		e = &elements[i];
		for (j = 0; j < e->num_rules; ++j) {
			r = &e->rules[j];
			free(r->rhs);
		}
		free(e->rules);
	}
	free(elements);
}

/*
 * The first is an 32-bit int that gives the # of elements.
 * The non-terminal elements follow according to their index in incr. order.
 * cc_token_type	(32-bit int)
 *  first, 32-bit int num_rules
 *  for each rule,
 *	  num_elements (32-bit int)
 *	  rhs elements array
 */
void serialize()
{
	int i, j;
	int fd;
	const struct element *e[2];
	const struct rule *r;
	enum cc_token_type type;

	fd = open("/tmp/grammar.bin", O_WRONLY | O_TRUNC | O_CREAT,
			  S_IRUSR | S_IWUSR);
	if (fd < 0) {
		perror("open");
		exit(errno);
	}
	assert(fd >= 0);

	for (i = j = 0; i < num_elements; ++i) {
		e[0] = &elements[i];
		if (!is_terminal(e[0]->type))
			++j;
	}
	write(fd, &j, sizeof(j));	/* # of non-terminals */

	/* Write in order of cc_token_type */
	e[0] = NULL;
	type = CC_TOKEN_TRANSLATION_OBJECT;
	while (type <= CC_TOKEN_FUNCTION_BODY) {
		for (i = 0; i < num_elements; ++i) {
			e[0] = &elements[i];
			if (is_terminal(e[0]->type))
				continue;
			if (e[0]->type != type)
				continue;
			break;
		}
		if (i == num_elements)
			break;
		write(fd, &e[0]->type, sizeof(e[0]->type));
		assert(e[0]->num_rules);
		write(fd, &e[0]->num_rules, sizeof(e[0]->num_rules));
		for (i = 0; i < e[0]->num_rules; ++i) {
			r = &e[0]->rules[i];
			assert(r->num_rhs);
			write(fd, &r->num_rhs, sizeof(r->num_rhs));
			/* Convert element index into type while writing */
			for (j = 0; j < r->num_rhs; ++j) {
				e[1] = &elements[r->rhs[j]];
				write(fd, &e[1]->type, sizeof(e[1]->type));
			}
		}
		++type;
	}
	close(fd);
}

int main(int argc, char **argv)
{
	int i, err, j, len;
	FILE *file;
	static char line[4096];
	struct element *e;
	struct rule *r;
	char *str;

	if (argc != 2) {
		fprintf(stderr, "%s: Usage: %s grammar.txt\n", __func__, argv[0]);
		return -1;
	}

	file = fopen(argv[1], "r");
	if (file == NULL) {
		fprintf(stderr, "%s: Error: Opening %s\n", __func__, argv[1]);
		return -1;
	}

	err = 0;
	while (fgets(line, 4096, file)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;

		len = strlen(line) - 1;	//	\n
		for (i = 0; i < len; ++i) {
			if (line[i] == ':')
				break;
		}
		str = calloc(i + 1, sizeof(char));
		str[i] = 0;
		memcpy(str, &line[0], i);
		assert(line[i] == ':');
		++i;

		j = add_element(str);
		e = &elements[j];
		free(str);
		e->rules = realloc(e->rules, (e->num_rules + 1) * sizeof(*r));
		r = &e->rules[e->num_rules++];
		memset(r, 0, sizeof(*r));
		for (; i < len;) {
			assert(line[i] == '\t');
			++i;
			assert(i < len);
			j = i;
			for (; i < len; ++i) {
				if (line[i] == '\t')
					break;
			}
			// [j, i) contains an element. i is either \t or len.
			str = calloc(i - j + 1, sizeof(char));
			str[i - j] = 0;
			memcpy(str, &line[j], i - j);
			r->rhs = realloc(r->rhs, (r->num_rhs + 1) * sizeof(int));
			r->rhs[r->num_rhs++] = add_element(str);
			free(str);
		}
	}
	fclose(file);
	//for (i = 0; i < num_elements; ++i)
	//	print_element(i);
	serialize();
	cleanup();
	return err;
}
