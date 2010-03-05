#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <isl_obj.h>
#include <isl_stream.h>
#include <barvinok/barvinok.h>

typedef void *(*isc_bin_op_fn)(void *lhs, void *rhs);
struct isc_bin_op {
	enum isl_token_type	op;
	isl_obj_type		lhs;
	isl_obj_type		rhs;
	isl_obj_type		res;
	isc_bin_op_fn		fn;
};

struct isc_bin_op bin_ops[] = {
	{ '+',	isl_obj_set,	isl_obj_set,
		isl_obj_set,
		(isc_bin_op_fn) &isl_set_union },
	{ '+',	isl_obj_map,	isl_obj_map,
		isl_obj_map,
		(isc_bin_op_fn) &isl_map_union },
	{ '-',	isl_obj_set,	isl_obj_set,
		isl_obj_set,
		(isc_bin_op_fn) &isl_set_subtract },
	{ '-',	isl_obj_map,	isl_obj_map,
		isl_obj_map,
		(isc_bin_op_fn) &isl_map_subtract },
	{ '*',	isl_obj_set,	isl_obj_set,
		isl_obj_set,
		(isc_bin_op_fn) &isl_set_intersect },
	{ '*',	isl_obj_map,	isl_obj_map,
		isl_obj_map,
		(isc_bin_op_fn) &isl_map_intersect },
	{ '+',	isl_obj_pw_qpolynomial,	isl_obj_pw_qpolynomial,
		isl_obj_pw_qpolynomial,
		(isc_bin_op_fn) &isl_pw_qpolynomial_add },
	{ '-',	isl_obj_pw_qpolynomial,	isl_obj_pw_qpolynomial,
		isl_obj_pw_qpolynomial,
		(isc_bin_op_fn) &isl_pw_qpolynomial_sub },
	{ '*',	isl_obj_pw_qpolynomial,	isl_obj_pw_qpolynomial,
		isl_obj_pw_qpolynomial,
		(isc_bin_op_fn) &isl_pw_qpolynomial_mul },
	0
};

__isl_give isl_set *set_sample(__isl_take isl_set *set)
{
	return isl_set_from_basic_set(isl_set_sample(set));
}

__isl_give isl_map *map_sample(__isl_take isl_map *map)
{
	return isl_map_from_basic_map(isl_map_sample(map));
}

typedef void *(*isc_un_op_fn)(void *arg);
struct isc_un_op {
	enum isl_token_type	op;
	isl_obj_type		arg;
	isl_obj_type		res;
	isc_un_op_fn		fn;
};
struct isc_named_un_op {
	char			*name;
	struct isc_un_op	op;
};
struct isc_named_un_op named_un_ops[] = {
	{"card",	{ -1,	isl_obj_set,	isl_obj_pw_qpolynomial,
		(isc_un_op_fn) &isl_set_card } },
	{"card",	{ -1,	isl_obj_map,	isl_obj_pw_qpolynomial,
		(isc_un_op_fn) &isl_map_card } },
	{"dom",	{ -1,	isl_obj_map,	isl_obj_set,
		(isc_un_op_fn) &isl_map_domain } },
	{"ran",	{ -1,	isl_obj_map,	isl_obj_set,
		(isc_un_op_fn) &isl_map_range } },
	{"lexmin",	{ -1,	isl_obj_map,	isl_obj_map,
		(isc_un_op_fn) &isl_map_lexmin } },
	{"lexmax",	{ -1,	isl_obj_map,	isl_obj_map,
		(isc_un_op_fn) &isl_map_lexmax } },
	{"lexmin",	{ -1,	isl_obj_set,	isl_obj_set,
		(isc_un_op_fn) &isl_set_lexmin } },
	{"lexmax",	{ -1,	isl_obj_set,	isl_obj_set,
		(isc_un_op_fn) &isl_set_lexmax } },
	{"sample",	{ -1,	isl_obj_set,	isl_obj_set,
		(isc_un_op_fn) &set_sample } },
	{"sample",	{ -1,	isl_obj_map,	isl_obj_map,
		(isc_un_op_fn) &map_sample } },
	{"sum",		{ -1,	isl_obj_pw_qpolynomial,	isl_obj_pw_qpolynomial,
		(isc_un_op_fn) &isl_pw_qpolynomial_sum } },
	NULL
};

struct isl_named_obj {
	char		*name;
	struct isl_obj	obj;
};

static void free_obj(struct isl_obj obj)
{
	obj.type->free(obj.v);
}

static int same_name(const void *entry, const void *val)
{
	const struct isl_named_obj *named = (const struct isl_named_obj *)entry;

	return !strcmp(named->name, val);
}

static int do_assign(struct isl_ctx *ctx, struct isl_hash_table *table,
	char *name, struct isl_obj obj)
{
	struct isl_hash_table_entry *entry;
	uint32_t name_hash;
	struct isl_named_obj *named;

	name_hash = isl_hash_string(isl_hash_init(), name);
	entry = isl_hash_table_find(ctx, table, name_hash, same_name, name, 1);
	if (!entry)
		goto error;
	if (entry->data) {
		named = entry->data;
		free_obj(named->obj);
		free(name);
	} else {
		named = isl_alloc_type(ctx, struct isl_named_obj);
		if (!named)
			goto error;
		named->name = name;
		entry->data = named;
	}
	named->obj = obj;

	return 0;
error:
	free_obj(obj);
	free(name);
	return -1;
}

static struct isl_obj stored_obj(struct isl_ctx *ctx,
	struct isl_hash_table *table, char *name)
{
	struct isl_obj obj = { isl_obj_none, NULL };
	struct isl_hash_table_entry *entry;
	uint32_t name_hash;

	name_hash = isl_hash_string(isl_hash_init(), name);
	entry = isl_hash_table_find(ctx, table, name_hash, same_name, name, 0);
	if (entry) {
		struct isl_named_obj *named;
		named = entry->data;
		obj = named->obj;
	}

	free(name);
	obj.v = obj.type->copy(obj.v);
	return obj;
}

static struct isc_bin_op *read_bin_op_if_available(struct isl_stream *s,
	isl_obj_type lhs)
{
	int i;
	struct isl_token *tok;

	tok = isl_stream_next_token(s);
	if (!tok)
		return NULL;

	for (i = 0; ; ++i) {
		if (!bin_ops[i].op)
			break;
		if (bin_ops[i].op != tok->type)
			continue;
		if (bin_ops[i].lhs != lhs)
			continue;

		isl_token_free(tok);
		return &bin_ops[i];
	}

	isl_stream_push_token(s, tok);

	return NULL;
}

static struct isc_un_op *read_prefix_un_op_if_available(struct isl_stream *s)
{
	int i;
	struct isl_token *tok;

	tok = isl_stream_next_token(s);
	if (!tok)
		return NULL;

	for (i = 0; ; ++i) {
		if (!named_un_ops[i].op.op)
			break;
		if (named_un_ops[i].op.op != tok->type)
			continue;

		isl_token_free(tok);
		return &named_un_ops[i].op;
	}

	isl_stream_push_token(s, tok);

	return NULL;
}

static struct isc_un_op *find_matching_un_op(struct isc_un_op *like,
	isl_obj_type arg)
{
	int i;

	for (i = 0; ; ++i) {
		if (!named_un_ops[i].op.op)
			break;
		if (named_un_ops[i].op.op != like->op)
			continue;
		if (named_un_ops[i].op.arg != arg)
			continue;

		return &named_un_ops[i].op;
	}

	return NULL;
}

static int is_assign(struct isl_stream *s)
{
	struct isl_token *tok;
	struct isl_token *tok2;
	int assign;

	tok = isl_stream_next_token(s);
	if (!tok)
		return 0;
	if (tok->type != ISL_TOKEN_IDENT) {
		isl_stream_push_token(s, tok);
		return 0;
	}

	tok2 = isl_stream_next_token(s);
	if (!tok2) {
		isl_stream_push_token(s, tok);
		return 0;
	}
	assign = tok2->type == ISL_TOKEN_DEF;
	isl_stream_push_token(s, tok2);
	isl_stream_push_token(s, tok);

	return assign;
}

static struct isl_obj read_obj(struct isl_stream *s,
	struct isl_hash_table *table);
static struct isl_obj read_expr(struct isl_stream *s,
	struct isl_hash_table *table);

static struct isl_obj read_un_op_expr(struct isl_stream *s,
	struct isl_hash_table *table, struct isc_un_op *op)
{
	struct isl_obj obj = { isl_obj_none, NULL };

	obj = read_obj(s, table);

	op = find_matching_un_op(op, obj.type);

	isl_assert(s->ctx, op, goto error);
	obj.v = op->fn(obj.v);
	obj.type = op->res;

	return obj;
error:
	free_obj(obj);
	obj.type = isl_obj_none;
	obj.v = NULL;
	return obj;
}

static struct isl_obj read_obj(struct isl_stream *s,
	struct isl_hash_table *table)
{
	struct isl_obj obj = { isl_obj_none, NULL };
	char *name = NULL;
	struct isc_un_op *op = NULL;

	if (isl_stream_eat_if_available(s, '(')) {
		obj = read_expr(s, table);
		if (isl_stream_eat(s, ')'))
			goto error;
		return obj;
	}

	op = read_prefix_un_op_if_available(s);
	if (op)
		return read_un_op_expr(s, table, op);

	name = isl_stream_read_ident_if_available(s);
	if (name) {
		obj = stored_obj(s->ctx, table, name);
	} else {
		obj = isl_stream_read_obj(s);
		assert(obj.v);
	}

	return obj;
error:
	free_obj(obj);
	obj.type = isl_obj_none;
	obj.v = NULL;
	return obj;
}

static struct isl_obj read_expr(struct isl_stream *s,
	struct isl_hash_table *table)
{
	struct isl_obj obj = { isl_obj_none, NULL };
	struct isl_obj right_obj = { isl_obj_none, NULL };

	obj = read_obj(s, table);
	for (;;) {
		struct isc_bin_op *op = NULL;

		op = read_bin_op_if_available(s, obj.type);
		if (!op)
			break;

		right_obj = read_obj(s, table);

		isl_assert(s->ctx, right_obj.type == op->rhs, goto error);
		obj.v = op->fn(obj.v, right_obj.v);
		obj.type = op->res;
	}

	return obj;
error:
	free_obj(right_obj);
	free_obj(obj);
	obj.type = isl_obj_none;
	obj.v = NULL;
	return obj;
}

static void read_line(struct isl_stream *s, struct isl_hash_table *table)
{
	struct isl_obj obj = { isl_obj_none, NULL };
	char *lhs = NULL;
	int assign = 0;
	struct isc_bin_op *op = NULL;

	if (isl_stream_is_empty(s))
		return;

	assign = is_assign(s);
	if (assign) {
		lhs = isl_stream_read_ident_if_available(s);
		if (isl_stream_eat(s, ISL_TOKEN_DEF))
			goto error;
	}

	obj = read_expr(s, table);
	if (obj.type == isl_obj_none || obj.v == NULL)
		goto error;
	if (isl_stream_eat(s, ';'))
		goto error;

	if (assign) {
		if (do_assign(s->ctx, table, lhs, obj))
			return;
	} else {
		obj.type->print(obj.v, stdout);
		free_obj(obj);
	}

	return;
error:
	free(lhs);
	free_obj(obj);
}

int free_cb(void *entry)
{
	struct isl_named_obj *named = entry;

	free_obj(named->obj);
	free(named->name);
	free(named);

	return 0;
}

static void register_named_ops(struct isl_stream *s)
{
	int i;

	for (i = 0; ; ++i) {
		if (!named_un_ops[i].name)
			break;
		named_un_ops[i].op.op = isl_stream_register_keyword(s,
							named_un_ops[i].name);
		assert(named_un_ops[i].op.op != ISL_TOKEN_ERROR);
	}
}

int main(int argc, char **argv)
{
	struct isl_ctx *ctx;
	struct isl_stream *s;
	struct isl_hash_table *table;

	ctx = isl_ctx_alloc();
	s = isl_stream_new_file(ctx, stdin);
	assert(s);
	table = isl_hash_table_alloc(ctx, 10);
	assert(table);

	register_named_ops(s);

	while (!feof(stdin)) {
		read_line(s, table);
	}

	isl_hash_table_foreach(ctx, table, free_cb);
	isl_hash_table_free(ctx, table);
	isl_stream_free(s);
	isl_ctx_free(ctx);

	return 0;
}