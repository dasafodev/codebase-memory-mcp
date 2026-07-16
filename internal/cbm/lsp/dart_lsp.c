/* Dart light semantic pass.  This is an original, syntax-driven resolver;
 * it does not embed or call the Dart analyzer. */
#include "dart_lsp.h"
#include "../helpers.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DART_EVAL_MAX_DEPTH 40
#define DART_IMPORT_INITIAL_CAP 8
#define DART_FIELD_INITIAL_CAP 16
#define DART_NAME_LIST_MAX 64
#define TS_FIELD(name) name, (uint32_t)(sizeof(name) - 1)

#define DART_FUNC_FLAG_CONSTRUCTOR (1 << 20)
#define DART_FUNC_FLAG_FACTORY (1 << 21)

#define DART_CONF_CONSTRUCTOR 0.95f
#define DART_CONF_TOP_LEVEL 0.95f
#define DART_CONF_METHOD 0.91f
#define DART_CONF_PROPERTY 0.90f
#define DART_CONF_STATIC 0.92f
#define DART_CONF_SUPER 0.88f

static void dart_walk_node(DartLSPContext *ctx, TSNode node);
static void dart_process_block(DartLSPContext *ctx, TSNode block, bool push_scope);
static const CBMType *dart_eval_chain(DartLSPContext *ctx, TSNode container);

static bool dart_is(TSNode node, const char *kind) {
    return !ts_node_is_null(node) && strcmp(ts_node_type(node), kind) == 0;
}

static char *dart_node_text(DartLSPContext *ctx, TSNode node) {
    if (!ctx || ts_node_is_null(node)) {
        return NULL;
    }
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (end < start || end > (uint32_t)ctx->source_len) {
        return NULL;
    }
    size_t len = (size_t)(end - start);
    char *out = (char *)cbm_arena_alloc(ctx->arena, len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, ctx->source + start, len);
    out[len] = '\0';
    return out;
}

static TSNode dart_named_child_kind(TSNode node, const char *kind) {
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (strcmp(ts_node_type(child), kind) == 0) {
            return child;
        }
    }
    TSNode none = {0};
    return none;
}

static TSNode dart_find_kind(TSNode node, const char *kind, int depth) {
    TSNode none = {0};
    if (ts_node_is_null(node) || depth < 0) {
        return none;
    }
    if (strcmp(ts_node_type(node), kind) == 0) {
        return node;
    }
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode hit = dart_find_kind(ts_node_named_child(node, i), kind, depth - 1);
        if (!ts_node_is_null(hit)) {
            return hit;
        }
    }
    return none;
}

static bool dart_has_token(TSNode node, const char *kind, int depth) {
    if (ts_node_is_null(node) || depth < 0) {
        return false;
    }
    if (strcmp(ts_node_type(node), kind) == 0) {
        return true;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        if (dart_has_token(ts_node_child(node, i), kind, depth - 1)) {
            return true;
        }
    }
    return false;
}

static bool dart_scope_contains(const CBMScope *scope, const char *name) {
    if (!name) {
        return false;
    }
    for (const CBMScope *current = scope; current; current = current->parent) {
        for (const CBMScopeChunk *chunk = current->chunks; chunk; chunk = chunk->next) {
            for (int i = 0; i < chunk->used; i++) {
                if (chunk->bindings[i].name && strcmp(chunk->bindings[i].name, name) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

static const char *dart_join(CBMArena *arena, const char *left, const char *right) {
    if (!left || !*left) {
        return right ? cbm_arena_strdup(arena, right) : NULL;
    }
    if (!right || !*right) {
        return cbm_arena_strdup(arena, left);
    }
    return cbm_arena_sprintf(arena, "%s.%s", left, right);
}

static const char *dart_short(const char *qn) {
    const char *dot = qn ? strrchr(qn, '.') : NULL;
    return dot ? dot + 1 : qn;
}

static const char *dart_type_qn(const CBMType *type) {
    if (!type) {
        return NULL;
    }
    if (type->kind == CBM_TYPE_NAMED) {
        return type->data.named.qualified_name;
    }
    if (type->kind == CBM_TYPE_TEMPLATE) {
        return type->data.template_type.template_name;
    }
    if (type->kind == CBM_TYPE_ALIAS) {
        return dart_type_qn(type->data.alias.underlying);
    }
    return NULL;
}

static const CBMType *dart_return_type(const CBMRegisteredFunc *func) {
    if (!func || !func->signature || func->signature->kind != CBM_TYPE_FUNC ||
        !func->signature->data.func.return_types) {
        return cbm_type_unknown();
    }
    const CBMType *ret = func->signature->data.func.return_types[0];
    return ret ? ret : cbm_type_unknown();
}

static const CBMType *dart_signature(CBMArena *arena, const CBMType *return_type) {
    const CBMType *returns[2] = {return_type ? return_type : cbm_type_unknown(), NULL};
    return cbm_type_func(arena, NULL, NULL, returns);
}

static void dart_emit(DartLSPContext *ctx, const char *callee_qn, const char *strategy,
                      float confidence) {
    if (!ctx || !ctx->resolved_calls || !ctx->enclosing_func_qn || !callee_qn ||
        confidence < 0.60f) {
        return;
    }
    CBMResolvedCall rc = {0};
    rc.caller_qn = ctx->enclosing_func_qn;
    rc.callee_qn = cbm_arena_strdup(ctx->arena, callee_qn);
    rc.strategy = strategy;
    rc.confidence = confidence;
    cbm_resolvedcall_push(ctx->resolved_calls, ctx->arena, rc);
    if (ctx->debug) {
        fprintf(stderr, "[dart_lsp] %s -> %s [%s %.2f]\n", ctx->enclosing_func_qn, callee_qn,
                strategy, (double)confidence);
    }
}

static void dart_seed_type(CBMTypeRegistry *registry, CBMArena *arena, const char *qn,
                           const char *parent) {
    CBMRegisteredType type = {0};
    type.qualified_name = qn;
    type.short_name = dart_short(qn);
    if (parent) {
        const char **parents = (const char **)cbm_arena_alloc(arena, 2 * sizeof(const char *));
        if (parents) {
            parents[0] = parent;
            parents[1] = NULL;
            type.embedded_types = parents;
        }
    }
    cbm_registry_add_type(registry, type);
}

static void dart_seed_method(CBMTypeRegistry *registry, CBMArena *arena, const char *receiver,
                             const char *name, const CBMType *return_type, int flags) {
    CBMRegisteredFunc func = {0};
    func.receiver_type = receiver;
    func.short_name = name;
    func.qualified_name = dart_join(arena, receiver, name);
    func.signature = dart_signature(arena, return_type);
    func.min_params = 0;
    func.flags = flags;
    cbm_registry_add_func(registry, func);
}

static void dart_seed_function(CBMTypeRegistry *registry, CBMArena *arena, const char *name,
                               const CBMType *return_type) {
    CBMRegisteredFunc func = {0};
    func.receiver_type = NULL;
    func.short_name = name;
    func.qualified_name = dart_join(arena, "dart.core", name);
    func.signature = dart_signature(arena, return_type);
    func.min_params = 0;
    cbm_registry_add_func(registry, func);
}

static void dart_seed_constructor(CBMTypeRegistry *registry, CBMArena *arena, const char *type_qn,
                                  bool is_factory) {
    CBMRegisteredFunc func = {0};
    func.receiver_type = type_qn;
    func.short_name = "<init>";
    func.qualified_name = type_qn;
    func.signature = dart_signature(arena, cbm_type_named(arena, type_qn));
    func.min_params = 0;
    func.flags = CBM_FUNC_FLAG_STATICMETHOD | DART_FUNC_FLAG_CONSTRUCTOR;
    if (is_factory) {
        func.flags |= DART_FUNC_FLAG_FACTORY;
    }
    cbm_registry_add_func(registry, func);
}

static void dart_seed_core(CBMTypeRegistry *registry, CBMArena *arena) {
    static const struct {
        const char *qn;
        const char *parent;
    } types[] = {{"dart.core.Object", NULL},
                 {"dart.core.String", "dart.core.Object"},
                 {"dart.core.num", "dart.core.Object"},
                 {"dart.core.int", "dart.core.num"},
                 {"dart.core.double", "dart.core.num"},
                 {"dart.core.bool", "dart.core.Object"},
                 {"dart.core.List", "dart.core.Iterable"},
                 {"dart.core.Map", "dart.core.Object"},
                 {"dart.core.Set", "dart.core.Iterable"},
                 {"dart.core.Iterable", "dart.core.Object"},
                 {"dart.core.Iterator", "dart.core.Object"},
                 {"dart.async.Future", "dart.core.Object"},
                 {"dart.async.Stream", "dart.core.Object"},
                 {"dart.core.Duration", "dart.core.Object"},
                 {"dart.core.DateTime", "dart.core.Object"},
                 {"dart.core.RegExp", "dart.core.Object"},
                 {"dart.core.Null", "dart.core.Object"},
                 {"dart.core.Never", "dart.core.Object"},
                 {NULL, NULL}};
    for (int i = 0; types[i].qn; i++) {
        dart_seed_type(registry, arena, types[i].qn, types[i].parent);
    }

    const CBMType *object_t = cbm_type_named(arena, "dart.core.Object");
    const CBMType *string_t = cbm_type_named(arena, "dart.core.String");
    const CBMType *int_t = cbm_type_named(arena, "dart.core.int");
    const CBMType *bool_t = cbm_type_named(arena, "dart.core.bool");
    const CBMType *void_t = cbm_type_named(arena, "dart.core.void");
    const CBMType *list_t = cbm_type_named(arena, "dart.core.List");
    const CBMType *iterable_t = cbm_type_named(arena, "dart.core.Iterable");
    const CBMType *iterator_t = cbm_type_named(arena, "dart.core.Iterator");
    const CBMType *future_t = cbm_type_named(arena, "dart.async.Future");

    dart_seed_method(registry, arena, "dart.core.Object", "toString", string_t, 0);
    dart_seed_method(registry, arena, "dart.core.Object", "hashCode", int_t,
                     CBM_FUNC_FLAG_PROPERTY);
    dart_seed_method(registry, arena, "dart.core.Object", "runtimeType", object_t,
                     CBM_FUNC_FLAG_PROPERTY);
    dart_seed_method(registry, arena, "dart.core.String", "toUpperCase", string_t, 0);
    dart_seed_method(registry, arena, "dart.core.String", "toLowerCase", string_t, 0);
    dart_seed_method(registry, arena, "dart.core.String", "trim", string_t, 0);
    dart_seed_method(registry, arena, "dart.core.String", "substring", string_t, 0);
    dart_seed_method(registry, arena, "dart.core.String", "replaceAll", string_t, 0);
    dart_seed_method(registry, arena, "dart.core.String", "contains", bool_t, 0);
    dart_seed_method(registry, arena, "dart.core.String", "startsWith", bool_t, 0);
    dart_seed_method(registry, arena, "dart.core.String", "endsWith", bool_t, 0);
    dart_seed_method(registry, arena, "dart.core.String", "length", int_t, CBM_FUNC_FLAG_PROPERTY);
    dart_seed_method(registry, arena, "dart.core.List", "add", void_t, 0);
    dart_seed_method(registry, arena, "dart.core.List", "addAll", void_t, 0);
    dart_seed_method(registry, arena, "dart.core.List", "map", iterable_t, 0);
    dart_seed_method(registry, arena, "dart.core.List", "where", iterable_t, 0);
    dart_seed_method(registry, arena, "dart.core.List", "length", int_t, CBM_FUNC_FLAG_PROPERTY);
    dart_seed_method(registry, arena, "dart.core.List", "first", object_t, CBM_FUNC_FLAG_PROPERTY);
    dart_seed_method(registry, arena, "dart.core.Iterable", "map", iterable_t, 0);
    dart_seed_method(registry, arena, "dart.core.Iterable", "where", iterable_t, 0);
    dart_seed_method(registry, arena, "dart.core.Iterable", "toList", list_t, 0);
    dart_seed_method(registry, arena, "dart.core.Iterable", "iterator", iterator_t,
                     CBM_FUNC_FLAG_PROPERTY);
    dart_seed_method(registry, arena, "dart.core.Map", "containsKey", bool_t, 0);
    dart_seed_method(registry, arena, "dart.core.Map", "length", int_t, CBM_FUNC_FLAG_PROPERTY);
    dart_seed_method(registry, arena, "dart.core.Set", "contains", bool_t, 0);
    dart_seed_method(registry, arena, "dart.async.Future", "then", future_t, 0);
    dart_seed_method(registry, arena, "dart.async.Stream", "listen", object_t, 0);
    dart_seed_method(registry, arena, "dart.core.DateTime", "now",
                     cbm_type_named(arena, "dart.core.DateTime"), CBM_FUNC_FLAG_STATICMETHOD);
    dart_seed_method(registry, arena, "dart.core.RegExp", "hasMatch", bool_t, 0);

    dart_seed_constructor(registry, arena, "dart.core.Object", false);
    dart_seed_constructor(registry, arena, "dart.core.Map", true);
    dart_seed_constructor(registry, arena, "dart.core.Set", true);
    dart_seed_constructor(registry, arena, "dart.async.Future", false);
    dart_seed_constructor(registry, arena, "dart.core.Duration", false);
    dart_seed_constructor(registry, arena, "dart.core.DateTime", false);
    dart_seed_constructor(registry, arena, "dart.core.RegExp", true);

    dart_seed_function(registry, arena, "print", void_t);
    dart_seed_function(registry, arena, "identical", bool_t);
}

void dart_lsp_init(DartLSPContext *ctx, CBMArena *arena, const char *source, int source_len,
                   const CBMTypeRegistry *registry, const char *module_qn, const char *project_name,
                   const char *rel_path, CBMResolvedCallArray *out) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->arena = arena;
    ctx->source = source;
    ctx->source_len = source_len;
    ctx->registry = registry;
    ctx->module_qn = module_qn ? cbm_arena_strdup(arena, module_qn) : "";
    ctx->project_name = project_name ? cbm_arena_strdup(arena, project_name) : "";
    ctx->rel_path = rel_path ? cbm_arena_strdup(arena, rel_path) : "";
    ctx->resolved_calls = out;
    ctx->current_scope = cbm_scope_push(arena, NULL);
    ctx->import_cap = DART_IMPORT_INITIAL_CAP;
    ctx->imports =
        (CBMDartImport *)cbm_arena_alloc(arena, (size_t)ctx->import_cap * sizeof(CBMDartImport));
    ctx->field_cap = DART_FIELD_INITIAL_CAP;
    ctx->fields =
        (CBMDartFieldInfo *)cbm_arena_alloc(arena, (size_t)ctx->field_cap * sizeof(*ctx->fields));
    ctx->debug = getenv("CBM_LSP_DEBUG") != NULL;
}

static const char *dart_uri_module(DartLSPContext *ctx, const char *uri) {
    if (!uri || !*uri) {
        return NULL;
    }
    const char *start = uri;
    size_t len = strlen(uri);
    if (len >= 2 &&
        ((*start == '\'' && uri[len - 1] == '\'') || (*start == '"' && uri[len - 1] == '"'))) {
        start++;
        len -= 2;
    }
    char *tmp = (char *)cbm_arena_alloc(ctx->arena, len + 1);
    if (!tmp) {
        return NULL;
    }
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    char *p = tmp;
    if (strncmp(p, "dart:", 5) == 0) {
        p += 5;
        for (char *q = p; *q; q++) {
            if (*q == '/') {
                *q = '.';
            }
        }
        return dart_join(ctx->arena, "dart", p);
    }
    if (strncmp(p, "package:", 8) == 0) {
        p += 8;
        char *slash = strchr(p, '/');
        if (!slash || slash == p || !slash[1]) {
            return NULL;
        }
        *slash = '\0';
        size_t path_len = strlen(slash + 1);
        char *rel_path = (char *)cbm_arena_alloc(ctx->arena, path_len + sizeof("lib/"));
        if (!rel_path) {
            return NULL;
        }
        memcpy(rel_path, "lib/", sizeof("lib/") - 1);
        memcpy(rel_path + sizeof("lib/") - 1, slash + 1, path_len + 1);
        return cbm_fqn_module(ctx->arena, p, rel_path);
    }
    if (strchr(p, ':')) {
        return NULL;
    }

    const char *base_path = ctx->rel_path;
    char *derived_path = NULL;
    if (!base_path || !*base_path) {
        const char *suffix = ctx->module_qn;
        size_t project_len = strlen(ctx->project_name);
        if (project_len > 0 && strncmp(suffix, ctx->project_name, project_len) == 0 &&
            suffix[project_len] == '.') {
            suffix += project_len + 1;
        }
        derived_path = cbm_arena_strdup(ctx->arena, suffix);
        if (!derived_path) {
            return NULL;
        }
        for (char *q = derived_path; *q; q++) {
            if (*q == '.') {
                *q = '/';
            }
        }
        base_path = derived_path;
    }

    const char *slash = strrchr(base_path, '/');
    size_t dir_len = slash ? (size_t)(slash - base_path) : 0;
    size_t uri_len = strlen(p);
    size_t joined_len = dir_len + (dir_len > 0 ? 1 : 0) + uri_len;
    char *joined = (char *)cbm_arena_alloc(ctx->arena, joined_len + 1);
    char **segments = (char **)cbm_arena_alloc(ctx->arena, (joined_len + 1) * sizeof(char *));
    if (!joined || !segments) {
        return NULL;
    }
    char *out = joined;
    if (dir_len > 0) {
        memcpy(out, base_path, dir_len);
        out += dir_len;
        *out++ = '/';
    }
    memcpy(out, p, uri_len + 1);

    int segment_count = 0;
    char *cursor = joined;
    while (*cursor) {
        while (*cursor == '/') {
            cursor++;
        }
        if (!*cursor) {
            break;
        }
        char *segment = cursor;
        while (*cursor && *cursor != '/') {
            cursor++;
        }
        if (*cursor) {
            *cursor++ = '\0';
        }
        if (strcmp(segment, ".") == 0) {
            continue;
        }
        if (strcmp(segment, "..") == 0) {
            if (segment_count == 0) {
                return NULL;
            }
            segment_count--;
            continue;
        }
        segments[segment_count++] = segment;
    }
    if (segment_count == 0) {
        return NULL;
    }
    out = joined;
    for (int i = 0; i < segment_count; i++) {
        if (i > 0) {
            *out++ = '/';
        }
        size_t segment_len = strlen(segments[i]);
        memmove(out, segments[i], segment_len);
        out += segment_len;
    }
    *out = '\0';
    return cbm_fqn_module(ctx->arena, ctx->project_name, joined);
}

static bool dart_name_in(const char **names, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (names[i] && strcmp(names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool dart_import_allows(const CBMDartImport *import, const char *name) {
    if (!import || !name) {
        return false;
    }
    if (import->show_count > 0 && !dart_name_in(import->show_names, import->show_count, name)) {
        return false;
    }
    return !dart_name_in(import->hide_names, import->hide_count, name);
}

static void dart_add_import(DartLSPContext *ctx, CBMDartImport import) {
    if (ctx->import_count >= ctx->import_cap) {
        int cap = ctx->import_cap * 2;
        CBMDartImport *items =
            (CBMDartImport *)cbm_arena_alloc(ctx->arena, (size_t)cap * sizeof(CBMDartImport));
        if (!items) {
            return;
        }
        memcpy(items, ctx->imports, (size_t)ctx->import_count * sizeof(CBMDartImport));
        ctx->imports = items;
        ctx->import_cap = cap;
    }
    ctx->imports[ctx->import_count++] = import;
}

static void dart_add_field(DartLSPContext *ctx, const char *owner_qn, const char *name,
                           const CBMType *type, bool is_static) {
    if (!ctx || !owner_qn || !name) {
        return;
    }
    if (ctx->field_count >= ctx->field_cap) {
        int cap = ctx->field_cap > 0 ? ctx->field_cap * 2 : DART_FIELD_INITIAL_CAP;
        CBMDartFieldInfo *items =
            (CBMDartFieldInfo *)cbm_arena_alloc(ctx->arena, (size_t)cap * sizeof(*items));
        if (!items) {
            return;
        }
        if (ctx->fields && ctx->field_count > 0) {
            memcpy(items, ctx->fields, (size_t)ctx->field_count * sizeof(*items));
        }
        ctx->fields = items;
        ctx->field_cap = cap;
    }
    CBMDartFieldInfo *field = &ctx->fields[ctx->field_count++];
    field->owner_qn = owner_qn;
    field->name = name;
    field->type = type ? type : cbm_type_unknown();
    field->is_static = is_static;
}

static const CBMDartFieldInfo *dart_find_direct_field(DartLSPContext *ctx, const char *owner_qn,
                                                      const char *name) {
    if (!ctx || !owner_qn || !name) {
        return NULL;
    }
    for (int i = 0; i < ctx->field_count; i++) {
        const CBMDartFieldInfo *field = &ctx->fields[i];
        if (strcmp(field->owner_qn, owner_qn) == 0 && strcmp(field->name, name) == 0) {
            return field;
        }
    }
    return NULL;
}

static void dart_parse_import(DartLSPContext *ctx, TSNode node) {
    TSNode spec = dart_find_kind(node, "import_specification", 4);
    TSNode literal = dart_find_kind(spec, "string_literal", 5);
    if (ts_node_is_null(spec) || ts_node_is_null(literal)) {
        return;
    }
    CBMDartImport import = {0};
    import.uri = dart_node_text(ctx, literal);
    import.module_qn = dart_uri_module(ctx, import.uri);

    const char *shows[DART_NAME_LIST_MAX];
    const char *hides[DART_NAME_LIST_MAX];
    int show_count = 0;
    int hide_count = 0;
    uint32_t count = ts_node_child_count(spec);
    bool saw_as = false;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(spec, i);
        const char *kind = ts_node_type(child);
        if (!ts_node_is_named(child) && strcmp(kind, "as") == 0) {
            saw_as = true;
            continue;
        }
        if (saw_as && dart_is(child, "identifier")) {
            import.prefix = dart_node_text(ctx, child);
            saw_as = false;
            continue;
        }
        if (!dart_is(child, "combinator")) {
            continue;
        }
        char *text = dart_node_text(ctx, child);
        bool is_show = text && strncmp(text, "show", 4) == 0;
        uint32_t nc = ts_node_named_child_count(child);
        for (uint32_t j = 0; j < nc; j++) {
            TSNode name = ts_node_named_child(child, j);
            if (!dart_is(name, "identifier")) {
                continue;
            }
            const char *value = dart_node_text(ctx, name);
            if (is_show && show_count < DART_NAME_LIST_MAX) {
                shows[show_count++] = value;
            } else if (!is_show && hide_count < DART_NAME_LIST_MAX) {
                hides[hide_count++] = value;
            }
        }
    }
    if (show_count > 0) {
        import.show_names =
            (const char **)cbm_arena_alloc(ctx->arena, (size_t)show_count * sizeof(const char *));
        if (import.show_names) {
            memcpy((void *)import.show_names, shows, (size_t)show_count * sizeof(const char *));
            import.show_count = show_count;
        }
    }
    if (hide_count > 0) {
        import.hide_names =
            (const char **)cbm_arena_alloc(ctx->arena, (size_t)hide_count * sizeof(const char *));
        if (import.hide_names) {
            memcpy((void *)import.hide_names, hides, (size_t)hide_count * sizeof(const char *));
            import.hide_count = hide_count;
        }
    }
    dart_add_import(ctx, import);
}

static bool dart_has_explicit_core_import(DartLSPContext *ctx) {
    for (int i = 0; i < ctx->import_count; i++) {
        if (ctx->imports[i].module_qn && strcmp(ctx->imports[i].module_qn, "dart.core") == 0) {
            return true;
        }
    }
    return false;
}

static char *dart_clean_type_name(DartLSPContext *ctx, const char *name) {
    if (!name) {
        return NULL;
    }
    while (isspace((unsigned char)*name)) {
        name++;
    }
    size_t len = strlen(name);
    while (len > 0 && isspace((unsigned char)name[len - 1])) {
        len--;
    }
    while (len > 0 && (name[len - 1] == '?' || name[len - 1] == '!')) {
        len--;
    }
    const char *lt = memchr(name, '<', len);
    if (lt) {
        len = (size_t)(lt - name);
    }
    char *clean = (char *)cbm_arena_alloc(ctx->arena, len + 1);
    if (!clean) {
        return NULL;
    }
    memcpy(clean, name, len);
    clean[len] = '\0';
    return clean;
}

const char *dart_resolve_class_name(DartLSPContext *ctx, const char *name) {
    char *clean = dart_clean_type_name(ctx, name);
    if (!clean || !*clean || strcmp(clean, "dynamic") == 0 || strcmp(clean, "void") == 0) {
        return NULL;
    }
    const CBMRegisteredType *exact = cbm_registry_lookup_type(ctx->registry, clean);
    if (exact) {
        return exact->qualified_name;
    }

    char *dot = strchr(clean, '.');
    if (dot) {
        *dot = '\0';
        const char *member = dot + 1;
        for (int i = 0; i < ctx->import_count; i++) {
            const CBMDartImport *import = &ctx->imports[i];
            if (!import->prefix || strcmp(import->prefix, clean) != 0 ||
                !dart_import_allows(import, member) || !import->module_qn) {
                continue;
            }
            const char *qn = dart_join(ctx->arena, import->module_qn, member);
            if (cbm_registry_lookup_type(ctx->registry, qn)) {
                return qn;
            }
        }
        *dot = '.';
        return NULL;
    }

    const char *local_qn = dart_join(ctx->arena, ctx->module_qn, clean);
    if (cbm_registry_lookup_type(ctx->registry, local_qn)) {
        return local_qn;
    }
    for (int i = 0; i < ctx->import_count; i++) {
        const CBMDartImport *import = &ctx->imports[i];
        if (import->prefix || !import->module_qn || !dart_import_allows(import, clean)) {
            continue;
        }
        const char *qn = dart_join(ctx->arena, import->module_qn, clean);
        if (cbm_registry_lookup_type(ctx->registry, qn)) {
            return qn;
        }
    }

    bool explicit_core = dart_has_explicit_core_import(ctx);
    bool core_allowed = !explicit_core;
    if (explicit_core) {
        for (int i = 0; i < ctx->import_count; i++) {
            const CBMDartImport *import = &ctx->imports[i];
            if (!import->prefix && import->module_qn &&
                strcmp(import->module_qn, "dart.core") == 0 && dart_import_allows(import, clean)) {
                core_allowed = true;
            }
        }
    }
    if (core_allowed) {
        static const char *async_types[] = {"Future", "Stream", NULL};
        const char *package = "dart.core";
        for (int i = 0; async_types[i]; i++) {
            if (strcmp(clean, async_types[i]) == 0) {
                package = "dart.async";
                break;
            }
        }
        const char *core_qn = dart_join(ctx->arena, package, clean);
        if (cbm_registry_lookup_type(ctx->registry, core_qn)) {
            return core_qn;
        }
    }

    return NULL;
}

const char *dart_resolve_function_name(DartLSPContext *ctx, const char *name) {
    if (!ctx || !name || !*name) {
        return NULL;
    }
    const char *local_qn = dart_join(ctx->arena, ctx->module_qn, name);
    const CBMRegisteredFunc *local = cbm_registry_lookup_func(ctx->registry, local_qn);
    if (local && !local->receiver_type) {
        return local->qualified_name;
    }
    for (int i = 0; i < ctx->import_count; i++) {
        const CBMDartImport *import = &ctx->imports[i];
        if (import->prefix || !import->module_qn || !dart_import_allows(import, name)) {
            continue;
        }
        const char *qn = dart_join(ctx->arena, import->module_qn, name);
        const CBMRegisteredFunc *func = cbm_registry_lookup_func(ctx->registry, qn);
        if (func && !func->receiver_type) {
            return func->qualified_name;
        }
    }
    bool explicit_core = dart_has_explicit_core_import(ctx);
    bool core_allowed = !explicit_core;
    if (explicit_core) {
        for (int i = 0; i < ctx->import_count; i++) {
            const CBMDartImport *import = &ctx->imports[i];
            if (!import->prefix && import->module_qn &&
                strcmp(import->module_qn, "dart.core") == 0 && dart_import_allows(import, name)) {
                core_allowed = true;
            }
        }
    }
    if (core_allowed) {
        const char *qn = dart_join(ctx->arena, "dart.core", name);
        const CBMRegisteredFunc *func = cbm_registry_lookup_func(ctx->registry, qn);
        if (func && !func->receiver_type) {
            return func->qualified_name;
        }
    }
    return NULL;
}

static const CBMRegisteredFunc *dart_lookup_method_depth(DartLSPContext *ctx, const char *class_qn,
                                                         const char *method_name, int depth) {
    if (!class_qn || depth >= CBM_LSP_MAX_LOOKUP_DEPTH) {
        return NULL;
    }
    const CBMRegisteredFunc *direct =
        cbm_registry_lookup_method_aliased(ctx->registry, class_qn, method_name);
    if (direct) {
        return direct;
    }
    const CBMRegisteredType *type = cbm_registry_lookup_type(ctx->registry, class_qn);
    if (!type || !type->embedded_types) {
        return NULL;
    }
    int count = 0;
    while (type->embedded_types[count]) {
        count++;
    }
    for (int i = count - 1; i >= 0; i--) {
        const CBMRegisteredFunc *hit =
            dart_lookup_method_depth(ctx, type->embedded_types[i], method_name, depth + 1);
        if (hit) {
            return hit;
        }
    }
    return NULL;
}

const CBMRegisteredFunc *dart_lookup_method(DartLSPContext *ctx, const char *class_qn,
                                            const char *method_name) {
    return dart_lookup_method_depth(ctx, class_qn, method_name, 0);
}

static const CBMType *dart_lookup_property_depth(DartLSPContext *ctx, const char *class_qn,
                                                 const char *property_name, int depth) {
    if (!class_qn || depth >= CBM_LSP_MAX_LOOKUP_DEPTH) {
        return cbm_type_unknown();
    }
    const CBMRegisteredType *type = cbm_registry_lookup_type(ctx->registry, class_qn);
    if (type && type->field_names && type->field_types) {
        for (int i = 0; type->field_names[i]; i++) {
            if (strcmp(type->field_names[i], property_name) == 0) {
                const CBMDartFieldInfo *field =
                    dart_find_direct_field(ctx, class_qn, property_name);
                if (field && field->is_static) {
                    return cbm_type_unknown();
                }
                return type->field_types[i] ? type->field_types[i] : cbm_type_unknown();
            }
        }
    }
    const CBMRegisteredFunc *getter =
        cbm_registry_lookup_method(ctx->registry, class_qn, property_name);
    if (getter && (getter->flags & CBM_FUNC_FLAG_PROPERTY)) {
        return (getter->flags & CBM_FUNC_FLAG_STATICMETHOD) ? cbm_type_unknown()
                                                            : dart_return_type(getter);
    }
    if (type && type->embedded_types) {
        int count = 0;
        while (type->embedded_types[count]) {
            count++;
        }
        for (int i = count - 1; i >= 0; i--) {
            const CBMType *hit =
                dart_lookup_property_depth(ctx, type->embedded_types[i], property_name, depth + 1);
            if (!cbm_type_is_unknown(hit)) {
                return hit;
            }
        }
    }
    return cbm_type_unknown();
}

const CBMType *dart_lookup_property_type(DartLSPContext *ctx, const char *class_qn,
                                         const char *property_name) {
    return dart_lookup_property_depth(ctx, class_qn, property_name, 0);
}

static const CBMType *dart_parse_type_parts(DartLSPContext *ctx, TSNode base, TSNode args) {
    char *name = dart_node_text(ctx, base);
    const char *qn = dart_resolve_class_name(ctx, name);
    if (!qn) {
        return cbm_type_unknown();
    }
    if (ts_node_is_null(args)) {
        return cbm_type_named(ctx->arena, qn);
    }
    const CBMType *type_args[32];
    int arg_count = 0;
    uint32_t count = ts_node_named_child_count(args);
    for (uint32_t i = 0; i < count && arg_count < 31; i++) {
        const CBMType *arg = dart_parse_type_node(ctx, ts_node_named_child(args, i));
        type_args[arg_count++] = arg;
    }
    return cbm_type_template(ctx->arena, qn, type_args, arg_count);
}

const CBMType *dart_parse_type_node(DartLSPContext *ctx, TSNode node) {
    if (!ctx || ts_node_is_null(node)) {
        return cbm_type_unknown();
    }
    const char *kind = ts_node_type(node);
    if (strcmp(kind, "void_type") == 0) {
        return cbm_type_named(ctx->arena, "dart.core.void");
    }
    if (strcmp(kind, "inferred_type") == 0 || strcmp(kind, "dynamic_type") == 0 ||
        strcmp(kind, "function_type") == 0) {
        return cbm_type_unknown();
    }
    if (strcmp(kind, "type_identifier") == 0 || strcmp(kind, "identifier") == 0) {
        return dart_parse_type_parts(ctx, node, (TSNode){0});
    }
    if (strcmp(kind, "nullable_type") == 0 || strcmp(kind, "type_not_void") == 0 ||
        strcmp(kind, "type") == 0) {
        TSNode base = dart_find_kind(node, "type_identifier", 5);
        TSNode args = dart_find_kind(node, "type_arguments", 5);
        if (!ts_node_is_null(base)) {
            return dart_parse_type_parts(ctx, base, args);
        }
    }
    TSNode base = dart_named_child_kind(node, "type_identifier");
    TSNode args = dart_named_child_kind(node, "type_arguments");
    if (!ts_node_is_null(base)) {
        return dart_parse_type_parts(ctx, base, args);
    }
    char *text = dart_node_text(ctx, node);
    const char *qn = dart_resolve_class_name(ctx, text);
    return qn ? cbm_type_named(ctx->arena, qn) : cbm_type_unknown();
}

static const CBMType *dart_decl_type(DartLSPContext *ctx, TSNode node) {
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *kind = ts_node_type(child);
        if (strcmp(kind, "type_identifier") == 0 || strcmp(kind, "void_type") == 0 ||
            strcmp(kind, "nullable_type") == 0 || strcmp(kind, "type_not_void") == 0) {
            TSNode args = {0};
            if (i + 1 < count &&
                strcmp(ts_node_type(ts_node_named_child(node, i + 1)), "type_arguments") == 0) {
                args = ts_node_named_child(node, i + 1);
            }
            return dart_parse_type_parts(ctx, child, args);
        }
    }
    return cbm_type_unknown();
}

static const char *dart_decl_name(DartLSPContext *ctx, TSNode node) {
    TSNode name = ts_node_child_by_field_name(node, TS_FIELD("name"));
    if (!ts_node_is_null(name)) {
        return dart_node_text(ctx, name);
    }
    TSNode inner = dart_named_child_kind(node, "function_signature");
    if (ts_node_is_null(inner)) {
        inner = dart_named_child_kind(node, "getter_signature");
    }
    if (ts_node_is_null(inner)) {
        inner = dart_named_child_kind(node, "setter_signature");
    }
    if (!ts_node_is_null(inner)) {
        name = ts_node_child_by_field_name(inner, TS_FIELD("name"));
        if (!ts_node_is_null(name)) {
            return dart_node_text(ctx, name);
        }
    }
    return NULL;
}

static const char *dart_type_decl_name(DartLSPContext *ctx, TSNode node) {
    TSNode name = ts_node_child_by_field_name(node, TS_FIELD("name"));
    if (ts_node_is_null(name)) {
        name = dart_named_child_kind(node, "identifier");
    }
    return ts_node_is_null(name) ? NULL : dart_node_text(ctx, name);
}

static bool dart_type_decl(TSNode node) {
    const char *kind = ts_node_type(node);
    return strcmp(kind, "class_definition") == 0 || strcmp(kind, "mixin_declaration") == 0 ||
           strcmp(kind, "enum_declaration") == 0;
}

static TSNode dart_type_body(TSNode node) {
    TSNode body = ts_node_child_by_field_name(node, TS_FIELD("body"));
    if (!ts_node_is_null(body)) {
        return body;
    }
    body = dart_named_child_kind(node, "class_body");
    if (ts_node_is_null(body)) {
        body = dart_named_child_kind(node, "enum_body");
    }
    return body;
}

static void dart_register_type_skeletons(DartLSPContext *ctx, TSNode root) {
    uint32_t count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        TSNode node = ts_node_named_child(root, i);
        if (!dart_type_decl(node)) {
            continue;
        }
        const char *name = dart_type_decl_name(ctx, node);
        if (!name) {
            continue;
        }
        CBMRegisteredType type = {0};
        type.qualified_name = dart_join(ctx->arena, ctx->module_qn, name);
        type.short_name = name;
        type.is_interface = dart_is(node, "mixin_declaration") ||
                            dart_is(node, "enum_declaration") ||
                            dart_has_token(node, "abstract", 2);
        cbm_registry_add_type((CBMTypeRegistry *)ctx->registry, type);
    }
}

static const char *dart_constructor_name(DartLSPContext *ctx, TSNode signature,
                                         const char *class_name) {
    char *text = dart_node_text(ctx, signature);
    char *params = text ? strchr(text, '(') : NULL;
    if (!params) {
        return NULL;
    }
    char *dot = NULL;
    for (char *p = text; p < params; p++) {
        if (*p == '.') {
            dot = p;
        }
    }
    if (!dot) {
        return NULL;
    }
    char *start = dot + 1;
    char *end = start;
    while (end < params && (isalnum((unsigned char)*end) || *end == '_' || *end == '$')) {
        end++;
    }
    if (end == start) {
        return NULL;
    }
    *end = '\0';
    (void)class_name;
    return start;
}

static void dart_register_constructor(DartLSPContext *ctx, const char *class_qn,
                                      const char *class_name, TSNode signature) {
    const char *name = dart_constructor_name(ctx, signature, class_name);
    CBMRegisteredFunc func = {0};
    func.receiver_type = class_qn;
    func.short_name = name ? name : "<init>";
    func.qualified_name = name ? dart_join(ctx->arena, class_qn, name) : class_qn;
    func.signature = dart_signature(ctx->arena, cbm_type_named(ctx->arena, class_qn));
    func.min_params = 0;
    func.flags = CBM_FUNC_FLAG_STATICMETHOD | DART_FUNC_FLAG_CONSTRUCTOR;
    const char *kind = ts_node_type(signature);
    if (strstr(kind, "factory_constructor_signature") != NULL) {
        func.flags |= DART_FUNC_FLAG_FACTORY;
    }
    cbm_registry_add_func((CBMTypeRegistry *)ctx->registry, func);
}

static void dart_register_callable(DartLSPContext *ctx, const char *owner_qn, TSNode signature,
                                   const char *forced_qn) {
    TSNode inner = signature;
    if (dart_is(signature, "method_signature")) {
        uint32_t count = ts_node_named_child_count(signature);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_named_child(signature, i);
            const char *kind = ts_node_type(child);
            if (strcmp(kind, "function_signature") == 0 || strcmp(kind, "getter_signature") == 0 ||
                strcmp(kind, "setter_signature") == 0 ||
                strcmp(kind, "factory_constructor_signature") == 0) {
                inner = child;
                break;
            }
        }
    }
    if (dart_is(inner, "factory_constructor_signature")) {
        const char *class_name = dart_short(owner_qn);
        dart_register_constructor(ctx, owner_qn, class_name, inner);
        return;
    }
    const char *name = dart_decl_name(ctx, inner);
    if (!name) {
        return;
    }
    const CBMType *return_type = dart_decl_type(ctx, inner);
    int flags = 0;
    if (dart_is(inner, "getter_signature")) {
        flags |= CBM_FUNC_FLAG_PROPERTY;
    }
    if (dart_has_token(signature, "static", 3)) {
        flags |= CBM_FUNC_FLAG_STATICMETHOD;
    }
    if (dart_is(inner, "setter_signature")) {
        return_type = cbm_type_named(ctx->arena, "dart.core.void");
    }
    CBMRegisteredFunc func = {0};
    func.receiver_type = owner_qn;
    func.short_name = name;
    func.qualified_name = forced_qn ? forced_qn
                                    : (owner_qn ? dart_join(ctx->arena, owner_qn, name)
                                                : dart_join(ctx->arena, ctx->module_qn, name));
    func.signature = dart_signature(ctx->arena, return_type);
    func.min_params = 0;
    func.flags = flags;
    cbm_registry_add_func((CBMTypeRegistry *)ctx->registry, func);
}

static int dart_collect_parent_nodes(TSNode node, TSNode *out, int count, int cap) {
    uint32_t nc = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < nc && count < cap; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *kind = ts_node_type(child);
        if (strcmp(kind, "type_identifier") == 0) {
            out[count++] = child;
        } else if (strcmp(kind, "mixins") == 0 || strcmp(kind, "interfaces") == 0 ||
                   strcmp(kind, "superclass") == 0) {
            count = dart_collect_parent_nodes(child, out, count, cap);
        }
    }
    return count;
}

static int dart_collect_decl_names(DartLSPContext *ctx, TSNode declaration, const char **names,
                                   const CBMType **types, int count, int cap,
                                   const CBMType *declared_type) {
    uint32_t nc = ts_node_named_child_count(declaration);
    for (uint32_t i = 0; i < nc && count < cap; i++) {
        TSNode child = ts_node_named_child(declaration, i);
        const char *kind = ts_node_type(child);
        bool item = strcmp(kind, "initialized_identifier") == 0 ||
                    strcmp(kind, "static_final_declaration") == 0 ||
                    strcmp(kind, "initialized_variable_definition") == 0;
        if (item) {
            TSNode name = ts_node_child_by_field_name(child, TS_FIELD("name"));
            if (ts_node_is_null(name)) {
                name = dart_named_child_kind(child, "identifier");
            }
            if (!ts_node_is_null(name)) {
                names[count] = dart_node_text(ctx, name);
                const CBMType *type = declared_type;
                if (cbm_type_is_unknown(type)) {
                    type = dart_eval_chain(ctx, child);
                }
                types[count] = type;
                count++;
            }
            continue;
        }
        if (strcmp(kind, "initialized_identifier_list") == 0 ||
            strcmp(kind, "static_final_declaration_list") == 0) {
            count = dart_collect_decl_names(ctx, child, names, types, count, cap, declared_type);
        }
    }
    return count;
}

static void dart_fill_type(DartLSPContext *ctx, TSNode node) {
    const char *name = dart_type_decl_name(ctx, node);
    if (!name) {
        return;
    }
    const char *qn = dart_join(ctx->arena, ctx->module_qn, name);
    CBMRegisteredType *type = (CBMRegisteredType *)cbm_registry_lookup_type(ctx->registry, qn);
    if (!type) {
        return;
    }

    TSNode parent_nodes[32];
    int parent_count = 0;
    uint32_t nc = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < nc && parent_count < 32; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *kind = ts_node_type(child);
        if (strcmp(kind, "superclass") == 0 || strcmp(kind, "interfaces") == 0) {
            parent_count = dart_collect_parent_nodes(child, parent_nodes, parent_count, 32);
        } else if (dart_is(node, "mixin_declaration") && strcmp(kind, "type_identifier") == 0) {
            parent_nodes[parent_count++] = child;
        }
    }
    if (parent_count > 0) {
        const char **parents = (const char **)cbm_arena_alloc(
            ctx->arena, (size_t)(parent_count + 1) * sizeof(const char *));
        int kept = 0;
        if (parents) {
            for (int i = 0; i < parent_count; i++) {
                char *raw = dart_node_text(ctx, parent_nodes[i]);
                const char *resolved = dart_resolve_class_name(ctx, raw);
                if (resolved && strcmp(resolved, qn) != 0) {
                    parents[kept++] = resolved;
                }
            }
            parents[kept] = NULL;
            type->embedded_types = kept > 0 ? parents : NULL;
        }
    }

    TSNode body = dart_type_body(node);
    const char *field_names[128];
    const CBMType *field_types[128];
    int field_count = 0;
    if (!ts_node_is_null(body)) {
        uint32_t body_count = ts_node_named_child_count(body);
        if (dart_is(node, "enum_declaration")) {
            for (uint32_t i = 0; i < body_count && field_count < 128; i++) {
                TSNode member = ts_node_named_child(body, i);
                if (!dart_is(member, "enum_constant")) {
                    continue;
                }
                TSNode enum_name = ts_node_child_by_field_name(member, TS_FIELD("name"));
                if (!ts_node_is_null(enum_name)) {
                    field_names[field_count] = dart_node_text(ctx, enum_name);
                    field_types[field_count] = cbm_type_named(ctx->arena, qn);
                    dart_add_field(ctx, qn, field_names[field_count], field_types[field_count],
                                   true);
                    field_count++;
                }
            }
        }
        for (uint32_t i = 0; i < body_count && field_count < 128; i++) {
            TSNode member = ts_node_named_child(body, i);
            if (!dart_is(member, "declaration")) {
                continue;
            }
            if (!ts_node_is_null(dart_find_kind(member, "constructor_signature", 2)) ||
                !ts_node_is_null(
                    dart_find_kind(member, "redirecting_factory_constructor_signature", 2))) {
                continue;
            }
            const CBMType *declared = dart_decl_type(ctx, member);
            int first_added = field_count;
            field_count = dart_collect_decl_names(ctx, member, field_names, field_types,
                                                  field_count, 128, declared);
            bool is_static = dart_has_token(member, "static", 3);
            for (int j = first_added; j < field_count; j++) {
                dart_add_field(ctx, qn, field_names[j], field_types[j], is_static);
            }
        }
    }
    if (field_count > 0) {
        const char **names = (const char **)cbm_arena_alloc(ctx->arena, (size_t)(field_count + 1) *
                                                                            sizeof(const char *));
        const CBMType **types = (const CBMType **)cbm_arena_alloc(
            ctx->arena, (size_t)(field_count + 1) * sizeof(const CBMType *));
        if (names && types) {
            for (int i = 0; i < field_count; i++) {
                names[i] = field_names[i];
                types[i] = field_types[i];
            }
            names[field_count] = NULL;
            types[field_count] = NULL;
            type->field_names = names;
            type->field_types = types;
        }
    }
}

static void dart_register_type_members(DartLSPContext *ctx, TSNode node) {
    const char *class_name = dart_type_decl_name(ctx, node);
    if (!class_name) {
        return;
    }
    const char *class_qn = dart_join(ctx->arena, ctx->module_qn, class_name);
    TSNode body = dart_type_body(node);
    if (ts_node_is_null(body)) {
        return;
    }
    bool saw_constructor = false;
    uint32_t count = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < count; i++) {
        TSNode member = ts_node_named_child(body, i);
        if (dart_is(member, "method_signature")) {
            if (!ts_node_is_null(dart_find_kind(member, "factory_constructor_signature", 3))) {
                saw_constructor = true;
            }
            dart_register_callable(ctx, class_qn, member, NULL);
            continue;
        }
        if (!dart_is(member, "declaration")) {
            continue;
        }
        TSNode declared_method = dart_find_kind(member, "function_signature", 2);
        if (ts_node_is_null(declared_method)) {
            declared_method = dart_find_kind(member, "getter_signature", 2);
        }
        if (ts_node_is_null(declared_method)) {
            declared_method = dart_find_kind(member, "setter_signature", 2);
        }
        if (!ts_node_is_null(declared_method)) {
            dart_register_callable(ctx, class_qn, declared_method, NULL);
            continue;
        }
        TSNode ctor = dart_find_kind(member, "constructor_signature", 3);
        if (ts_node_is_null(ctor)) {
            ctor = dart_find_kind(member, "constant_constructor_signature", 3);
        }
        if (!ts_node_is_null(ctor)) {
            saw_constructor = true;
            dart_register_constructor(ctx, class_qn, class_name, ctor);
            continue;
        }
        TSNode redirect = dart_find_kind(member, "redirecting_factory_constructor_signature", 3);
        if (!ts_node_is_null(redirect)) {
            saw_constructor = true;
            dart_register_constructor(ctx, class_qn, class_name, redirect);
        }
    }
    const CBMRegisteredType *type = cbm_registry_lookup_type(ctx->registry, class_qn);
    if (!saw_constructor && dart_is(node, "class_definition") && type && !type->is_interface) {
        dart_seed_constructor((CBMTypeRegistry *)ctx->registry, ctx->arena, class_qn, false);
    }
}

static void dart_register_extension(DartLSPContext *ctx, TSNode node) {
    TSNode receiver_node = ts_node_child_by_field_name(node, TS_FIELD("class"));
    const CBMType *receiver = dart_parse_type_node(ctx, receiver_node);
    const char *receiver_qn = dart_type_qn(receiver);
    if (!receiver_qn) {
        return;
    }
    const char *extension_name = dart_decl_name(ctx, node);
    if (!extension_name) {
        extension_name = "extension";
    }
    const char *extension_qn = dart_join(ctx->arena, ctx->module_qn, extension_name);
    TSNode body = ts_node_child_by_field_name(node, TS_FIELD("body"));
    if (ts_node_is_null(body)) {
        body = dart_named_child_kind(node, "extension_body");
    }
    uint32_t count = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < count; i++) {
        TSNode member = ts_node_named_child(body, i);
        if (!dart_is(member, "method_signature")) {
            continue;
        }
        const char *name = dart_decl_name(ctx, member);
        if (!name) {
            continue;
        }
        const char *qn = dart_join(ctx->arena, extension_qn, name);
        dart_register_callable(ctx, receiver_qn, member, qn);
    }
}

static void dart_register_top_level(DartLSPContext *ctx, TSNode root) {
    uint32_t count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        TSNode node = ts_node_named_child(root, i);
        const char *kind = ts_node_type(node);
        if (strcmp(kind, "function_signature") == 0 || strcmp(kind, "getter_signature") == 0 ||
            strcmp(kind, "setter_signature") == 0) {
            dart_register_callable(ctx, NULL, node, NULL);
        } else if (strcmp(kind, "extension_declaration") == 0) {
            dart_register_extension(ctx, node);
        }
    }
}

static void dart_build_registry(DartLSPContext *ctx, TSNode root) {
    uint32_t count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        TSNode node = ts_node_named_child(root, i);
        if (dart_is(node, "import_or_export")) {
            dart_parse_import(ctx, node);
        }
    }
    dart_register_type_skeletons(ctx, root);
    for (uint32_t i = 0; i < count; i++) {
        TSNode node = ts_node_named_child(root, i);
        if (dart_type_decl(node)) {
            dart_register_type_members(ctx, node);
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        TSNode node = ts_node_named_child(root, i);
        if (dart_type_decl(node)) {
            dart_fill_type(ctx, node);
        }
    }
    dart_register_top_level(ctx, root);
}

static const CBMDartImport *dart_import_for_prefix(DartLSPContext *ctx, const char *prefix) {
    for (int i = 0; i < ctx->import_count; i++) {
        if (ctx->imports[i].prefix && strcmp(ctx->imports[i].prefix, prefix) == 0) {
            return &ctx->imports[i];
        }
    }
    return NULL;
}

static const char *dart_selector_member(DartLSPContext *ctx, TSNode selector) {
    TSNode name = dart_find_kind(selector, "identifier", 3);
    return ts_node_is_null(name) ? NULL : dart_node_text(ctx, name);
}

static TSNode dart_selector_arguments(TSNode selector) {
    TSNode arguments = dart_find_kind(selector, "arguments", 4);
    if (!ts_node_is_null(arguments)) {
        return arguments;
    }
    return dart_find_kind(selector, "argument_part", 4);
}

static bool dart_selector_is_bang(DartLSPContext *ctx, TSNode selector) {
    char *text = dart_node_text(ctx, selector);
    return text && strcmp(text, "!") == 0;
}

static void dart_eval_arguments(DartLSPContext *ctx, TSNode arguments) {
    if (ts_node_is_null(arguments)) {
        return;
    }
    if (dart_is(arguments, "argument_part")) {
        TSNode nested = dart_find_kind(arguments, "arguments", 4);
        if (!ts_node_is_null(nested)) {
            dart_eval_arguments(ctx, nested);
            return;
        }
    }
    uint32_t count = ts_node_named_child_count(arguments);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(arguments, i);
        if (dart_is(child, "type_arguments")) {
            continue;
        }
        if (dart_is(child, "argument_part") || dart_is(child, "argument_list")) {
            dart_eval_arguments(ctx, child);
        } else {
            (void)dart_eval_expr_type(ctx, child);
        }
    }
}

static const CBMType *dart_literal_type(DartLSPContext *ctx, TSNode node) {
    const char *kind = ts_node_type(node);
    if (strcmp(kind, "string_literal") == 0 || strcmp(kind, "string_interpolation") == 0 ||
        strcmp(kind, "symbol_literal") == 0) {
        return cbm_type_named(ctx->arena, "dart.core.String");
    }
    if (strcmp(kind, "decimal_integer_literal") == 0 || strcmp(kind, "hex_integer_literal") == 0 ||
        strcmp(kind, "integer_literal") == 0) {
        return cbm_type_named(ctx->arena, "dart.core.int");
    }
    if (strcmp(kind, "decimal_floating_point_literal") == 0 ||
        strcmp(kind, "double_literal") == 0) {
        return cbm_type_named(ctx->arena, "dart.core.double");
    }
    if (strcmp(kind, "boolean_literal") == 0) {
        return cbm_type_named(ctx->arena, "dart.core.bool");
    }
    return cbm_type_unknown();
}

static const CBMType *dart_collection_literal_type(DartLSPContext *ctx, TSNode node) {
    const char *kind = ts_node_type(node);
    const char *base_qn = NULL;
    if (strcmp(kind, "list_literal") == 0) {
        base_qn = "dart.core.List";
    } else if (strcmp(kind, "set_or_map_literal") == 0) {
        base_qn = ts_node_is_null(dart_named_child_kind(node, "pair")) ? "dart.core.Set"
                                                                       : "dart.core.Map";
    }
    if (!base_qn) {
        return cbm_type_unknown();
    }
    const CBMType *args[2];
    int arg_count = 0;
    TSNode type_args = dart_named_child_kind(node, "type_arguments");
    if (!ts_node_is_null(type_args)) {
        uint32_t count = ts_node_named_child_count(type_args);
        for (uint32_t i = 0; i < count && arg_count < 2; i++) {
            args[arg_count++] = dart_parse_type_node(ctx, ts_node_named_child(type_args, i));
        }
    }
    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!dart_is(child, "type_arguments")) {
            (void)dart_eval_expr_type(ctx, child);
        }
    }
    return cbm_type_template(ctx->arena, base_qn, args, arg_count);
}

static const CBMType *dart_identifier_type(DartLSPContext *ctx, TSNode node, bool *is_class_ref,
                                           const char **module_qn) {
    char *name = dart_node_text(ctx, node);
    if (!name) {
        return cbm_type_unknown();
    }
    if (strcmp(name, "true") == 0 || strcmp(name, "false") == 0) {
        return cbm_type_named(ctx->arena, "dart.core.bool");
    }
    if (strcmp(name, "null") == 0) {
        return cbm_type_named(ctx->arena, "dart.core.Null");
    }
    const CBMType *bound = cbm_scope_lookup(ctx->current_scope, name);
    if (!cbm_type_is_unknown(bound)) {
        return bound;
    }
    const CBMDartImport *import = dart_import_for_prefix(ctx, name);
    if (import && import->module_qn) {
        if (module_qn) {
            *module_qn = import->module_qn;
        }
        return cbm_type_module(ctx->arena, import->module_qn);
    }
    const char *class_qn = dart_resolve_class_name(ctx, name);
    if (class_qn) {
        if (is_class_ref) {
            *is_class_ref = true;
        }
        return cbm_type_named(ctx->arena, class_qn);
    }
    const char *getter_qn = dart_resolve_function_name(ctx, name);
    const CBMRegisteredFunc *getter =
        getter_qn ? cbm_registry_lookup_func(ctx->registry, getter_qn) : NULL;
    if (getter && (getter->flags & CBM_FUNC_FLAG_PROPERTY)) {
        dart_emit(ctx, getter->qualified_name, "lsp_dart_getter", DART_CONF_PROPERTY);
        return dart_return_type(getter);
    }
    return cbm_type_unknown();
}

static const CBMType *dart_eval_base(DartLSPContext *ctx, TSNode node, bool *is_class_ref,
                                     const char **module_qn) {
    if (ts_node_is_null(node)) {
        return cbm_type_unknown();
    }
    const char *kind = ts_node_type(node);
    if (strcmp(kind, "identifier") == 0 || strcmp(kind, "type_identifier") == 0) {
        return dart_identifier_type(ctx, node, is_class_ref, module_qn);
    }
    if (strcmp(kind, "this") == 0) {
        return ctx->this_type ? ctx->this_type : cbm_type_unknown();
    }
    if (strcmp(kind, "super") == 0) {
        return ctx->super_type ? ctx->super_type : cbm_type_unknown();
    }
    const CBMType *literal = dart_literal_type(ctx, node);
    if (!cbm_type_is_unknown(literal)) {
        return literal;
    }
    if (strcmp(kind, "list_literal") == 0 || strcmp(kind, "set_or_map_literal") == 0) {
        return dart_collection_literal_type(ctx, node);
    }
    return dart_eval_expr_type(ctx, node);
}

static int dart_value_parts(TSNode container, TSNode *parts, int cap) {
    int count = 0;
    const char *kind = ts_node_type(container);
    bool fields_only = strcmp(kind, "initialized_variable_definition") == 0 ||
                       strcmp(kind, "initialized_identifier") == 0 ||
                       strcmp(kind, "static_final_declaration") == 0;
    uint32_t total = ts_node_child_count(container);
    for (uint32_t i = 0; i < total && count < cap; i++) {
        TSNode child = ts_node_child(container, i);
        if (!ts_node_is_named(child)) {
            continue;
        }
        const char *field = ts_node_field_name_for_child(container, i);
        if (fields_only && (!field || strcmp(field, "value") != 0)) {
            continue;
        }
        if (!fields_only) {
            const char *child_kind = ts_node_type(child);
            if ((strcmp(kind, "const_object_expression") == 0 ||
                 strcmp(kind, "new_expression") == 0) &&
                (strcmp(child_kind, "const_builtin") == 0 ||
                 strcmp(child_kind, "new_builtin") == 0)) {
                continue;
            }
            if (strcmp(child_kind, "type_cast") == 0) {
                parts[count++] = child;
                continue;
            }
            if (strcmp(kind, "function_body") == 0 && strcmp(child_kind, "block") == 0) {
                continue;
            }
        }
        parts[count++] = child;
    }
    return count;
}

static bool dart_part_is_call_selector(TSNode node) {
    return (dart_is(node, "selector") && !ts_node_is_null(dart_selector_arguments(node))) ||
           dart_is(node, "arguments") || dart_is(node, "argument_part");
}

static bool dart_part_is_member(TSNode node) {
    return dart_is(node, "selector") || dart_is(node, "unconditional_assignable_selector") ||
           dart_is(node, "conditional_assignable_selector") || dart_is(node, "cascade_selector");
}

static const CBMType *dart_apply_property(DartLSPContext *ctx, const CBMType *receiver,
                                          const char *base_name, const char *member,
                                          const char *module_qn, bool is_class_ref,
                                          bool *result_is_class_ref) {
    if (result_is_class_ref) {
        *result_is_class_ref = false;
    }
    if (!member) {
        return cbm_type_unknown();
    }
    if (module_qn) {
        const CBMDartImport *import = base_name ? dart_import_for_prefix(ctx, base_name) : NULL;
        if (import && !dart_import_allows(import, member)) {
            return cbm_type_unknown();
        }
        const char *qn = dart_join(ctx->arena, module_qn, member);
        if (cbm_registry_lookup_type(ctx->registry, qn)) {
            if (result_is_class_ref) {
                *result_is_class_ref = true;
            }
            return cbm_type_named(ctx->arena, qn);
        }
        const CBMRegisteredFunc *getter = cbm_registry_lookup_func(ctx->registry, qn);
        if (getter && (getter->flags & CBM_FUNC_FLAG_PROPERTY)) {
            dart_emit(ctx, getter->qualified_name, "lsp_dart_import_getter", DART_CONF_PROPERTY);
            return dart_return_type(getter);
        }
        return cbm_type_unknown();
    }
    const char *receiver_qn = dart_type_qn(receiver);
    if (!receiver_qn) {
        return cbm_type_unknown();
    }
    const CBMRegisteredFunc *getter =
        is_class_ref ? cbm_registry_lookup_method_aliased(ctx->registry, receiver_qn, member)
                     : dart_lookup_method(ctx, receiver_qn, member);
    if (getter && (getter->flags & CBM_FUNC_FLAG_PROPERTY)) {
        bool getter_is_static = (getter->flags & CBM_FUNC_FLAG_STATICMETHOD) != 0;
        if (getter_is_static != is_class_ref) {
            return cbm_type_unknown();
        }
        dart_emit(ctx, getter->qualified_name, "lsp_dart_getter", DART_CONF_PROPERTY);
        return dart_return_type(getter);
    }
    if (is_class_ref) {
        const CBMDartFieldInfo *field = dart_find_direct_field(ctx, receiver_qn, member);
        return field && field->is_static ? field->type : cbm_type_unknown();
    }
    const CBMType *field = dart_lookup_property_type(ctx, receiver_qn, member);
    if (!cbm_type_is_unknown(field)) {
        return field;
    }
    return cbm_type_unknown();
}

static const CBMType *dart_apply_call(DartLSPContext *ctx, const CBMType *receiver,
                                      const char *base_name, const char *member,
                                      const char *module_qn, bool is_class_ref, bool was_this,
                                      bool was_super, TSNode arguments) {
    dart_eval_arguments(ctx, arguments);
    if (member) {
        if (module_qn) {
            const CBMDartImport *import = base_name ? dart_import_for_prefix(ctx, base_name) : NULL;
            if (import && !dart_import_allows(import, member)) {
                return cbm_type_unknown();
            }
            const char *qn = dart_join(ctx->arena, module_qn, member);
            const CBMRegisteredFunc *func = cbm_registry_lookup_func(ctx->registry, qn);
            if (func && !func->receiver_type) {
                dart_emit(ctx, func->qualified_name, "lsp_dart_import", DART_CONF_TOP_LEVEL);
                return dart_return_type(func);
            }
            const CBMRegisteredType *type = cbm_registry_lookup_type(ctx->registry, qn);
            if (type && func && (func->flags & DART_FUNC_FLAG_CONSTRUCTOR) &&
                (!type->is_interface || (func->flags & DART_FUNC_FLAG_FACTORY))) {
                dart_emit(ctx, qn, "lsp_dart_constructor", DART_CONF_CONSTRUCTOR);
                return cbm_type_named(ctx->arena, qn);
            }
            return cbm_type_unknown();
        }
        const char *receiver_qn = dart_type_qn(receiver);
        if (!receiver_qn) {
            return cbm_type_unknown();
        }
        const CBMRegisteredFunc *func =
            is_class_ref ? cbm_registry_lookup_method_aliased(ctx->registry, receiver_qn, member)
                         : dart_lookup_method(ctx, receiver_qn, member);
        if (!func) {
            return cbm_type_unknown();
        }
        bool func_is_static = (func->flags & CBM_FUNC_FLAG_STATICMETHOD) != 0;
        if (func_is_static != is_class_ref) {
            return cbm_type_unknown();
        }
        const char *strategy = "lsp_dart_method";
        float confidence = DART_CONF_METHOD;
        if (was_super) {
            strategy = "lsp_dart_super";
            confidence = DART_CONF_SUPER;
        } else if (was_this) {
            strategy = "lsp_dart_this";
        } else if (is_class_ref) {
            if (func->flags & DART_FUNC_FLAG_CONSTRUCTOR) {
                const CBMRegisteredType *type =
                    cbm_registry_lookup_type(ctx->registry, receiver_qn);
                if (type && type->is_interface && !(func->flags & DART_FUNC_FLAG_FACTORY)) {
                    return cbm_type_unknown();
                }
                strategy = "lsp_dart_constructor";
                confidence = DART_CONF_CONSTRUCTOR;
            } else {
                strategy = "lsp_dart_static";
                confidence = DART_CONF_STATIC;
            }
        }
        dart_emit(ctx, func->qualified_name, strategy, confidence);
        return dart_return_type(func);
    }
    if (is_class_ref) {
        const char *class_qn = dart_type_qn(receiver);
        if (class_qn) {
            const CBMRegisteredFunc *ctor = cbm_registry_lookup_func(ctx->registry, class_qn);
            const CBMRegisteredType *type = cbm_registry_lookup_type(ctx->registry, class_qn);
            if (ctor && (ctor->flags & DART_FUNC_FLAG_CONSTRUCTOR) &&
                (!type || !type->is_interface || (ctor->flags & DART_FUNC_FLAG_FACTORY))) {
                dart_emit(ctx, class_qn, "lsp_dart_constructor", DART_CONF_CONSTRUCTOR);
                return cbm_type_named(ctx->arena, class_qn);
            }
        }
        return cbm_type_unknown();
    }
    if (base_name && !module_qn && !dart_scope_contains(ctx->current_scope, base_name)) {
        const char *qn = dart_resolve_function_name(ctx, base_name);
        const CBMRegisteredFunc *func = qn ? cbm_registry_lookup_func(ctx->registry, qn) : NULL;
        if (func && !func->receiver_type) {
            dart_emit(ctx, func->qualified_name, "lsp_dart_top_level", DART_CONF_TOP_LEVEL);
            return dart_return_type(func);
        }
    }
    return cbm_type_unknown();
}

static void dart_eval_cascade(DartLSPContext *ctx, TSNode section, const CBMType *base_type) {
    const char *member = NULL;
    TSNode arguments = {0};
    uint32_t count = ts_node_named_child_count(section);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(section, i);
        if (dart_is(child, "cascade_selector")) {
            member = dart_selector_member(ctx, child);
        } else if (dart_is(child, "argument_part")) {
            arguments = dart_find_kind(child, "arguments", 2);
            if (ts_node_is_null(arguments)) {
                arguments = child;
            }
        }
    }
    if (member && !ts_node_is_null(arguments)) {
        (void)dart_apply_call(ctx, base_type, NULL, member, NULL, false, false, false, arguments);
    } else if (member) {
        (void)dart_apply_property(ctx, base_type, NULL, member, NULL, false, NULL);
    }
}

static const CBMType *dart_eval_chain(DartLSPContext *ctx, TSNode container) {
    TSNode parts[256];
    int count = dart_value_parts(container, parts, 256);
    if (count == 0) {
        return cbm_type_unknown();
    }
    if (count == 1 && !dart_is(parts[0], "selector") && !dart_is(parts[0], "cascade_section")) {
        return dart_eval_expr_type(ctx, parts[0]);
    }

    bool is_class_ref = false;
    const char *module_qn = NULL;
    TSNode base = parts[0];
    const CBMType *current = dart_eval_base(ctx, base, &is_class_ref, &module_qn);
    const CBMType *cascade_base = current;
    char *base_name = (dart_is(base, "identifier") || dart_is(base, "type_identifier"))
                          ? dart_node_text(ctx, base)
                          : NULL;
    bool was_this = dart_is(base, "this");
    bool was_super = dart_is(base, "super");
    const char *pending_member = NULL;

    for (int i = 1; i < count; i++) {
        TSNode part = parts[i];
        if (dart_is(part, "cascade_section")) {
            dart_eval_cascade(ctx, part, cascade_base);
            current = cascade_base;
            pending_member = NULL;
            continue;
        }
        if (dart_is(part, "type_cast")) {
            TSNode type = dart_find_kind(part, "type_identifier", 3);
            if (!ts_node_is_null(type)) {
                current = dart_parse_type_node(ctx, type);
            }
            continue;
        }
        if (dart_is(part, "arguments") || dart_is(part, "argument_part")) {
            TSNode arguments =
                dart_is(part, "arguments") ? part : dart_find_kind(part, "arguments", 2);
            if (ts_node_is_null(arguments)) {
                arguments = part;
            }
            current = dart_apply_call(ctx, current, base_name, pending_member, module_qn,
                                      is_class_ref, was_this, was_super, arguments);
            pending_member = NULL;
            module_qn = NULL;
            is_class_ref = false;
            was_this = false;
            was_super = false;
            continue;
        }
        if (!dart_part_is_member(part)) {
            continue;
        }
        if (dart_is(part, "selector") && dart_selector_is_bang(ctx, part)) {
            continue;
        }
        TSNode arguments = dart_is(part, "selector") ? dart_selector_arguments(part) : (TSNode){0};
        if (!ts_node_is_null(arguments)) {
            current = dart_apply_call(ctx, current, base_name, pending_member, module_qn,
                                      is_class_ref, was_this, was_super, arguments);
            pending_member = NULL;
            module_qn = NULL;
            is_class_ref = false;
            was_this = false;
            was_super = false;
            continue;
        }
        const char *member = dart_selector_member(ctx, part);
        if (!member) {
            continue;
        }
        bool followed_by_call = i + 1 < count && dart_part_is_call_selector(parts[i + 1]);
        if (followed_by_call) {
            pending_member = member;
        } else {
            bool property_is_class_ref = false;
            current = dart_apply_property(ctx, current, base_name, member, module_qn, is_class_ref,
                                          &property_is_class_ref);
            pending_member = NULL;
            module_qn = NULL;
            is_class_ref = property_is_class_ref;
            was_this = false;
            was_super = false;
        }
    }
    return current ? current : cbm_type_unknown();
}

static const CBMType *dart_unwrap_future(const CBMType *type) {
    if (type && type->kind == CBM_TYPE_TEMPLATE && type->data.template_type.template_name &&
        strcmp(type->data.template_type.template_name, "dart.async.Future") == 0 &&
        type->data.template_type.arg_count > 0 && type->data.template_type.template_args) {
        return type->data.template_type.template_args[0];
    }
    return cbm_type_unknown();
}

static bool dart_has_direct_chain(TSNode node) {
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        const char *kind = ts_node_type(ts_node_named_child(node, i));
        if (strcmp(kind, "selector") == 0 || strcmp(kind, "cascade_section") == 0) {
            return true;
        }
    }
    return false;
}

const CBMType *dart_eval_expr_type(DartLSPContext *ctx, TSNode node) {
    if (!ctx || ts_node_is_null(node) || ctx->eval_depth >= DART_EVAL_MAX_DEPTH) {
        return cbm_type_unknown();
    }
    ctx->eval_depth++;
    const CBMType *result = cbm_type_unknown();
    const char *kind = ts_node_type(node);

    const CBMType *literal = dart_literal_type(ctx, node);
    if (!cbm_type_is_unknown(literal)) {
        result = literal;
        goto done;
    }
    if (strcmp(kind, "identifier") == 0 || strcmp(kind, "type_identifier") == 0) {
        result = dart_identifier_type(ctx, node, NULL, NULL);
        goto done;
    }
    if (strcmp(kind, "this") == 0) {
        result = ctx->this_type ? ctx->this_type : cbm_type_unknown();
        goto done;
    }
    if (strcmp(kind, "super") == 0) {
        result = ctx->super_type ? ctx->super_type : cbm_type_unknown();
        goto done;
    }
    if (strcmp(kind, "list_literal") == 0 || strcmp(kind, "set_or_map_literal") == 0) {
        result = dart_collection_literal_type(ctx, node);
        goto done;
    }
    if (strcmp(kind, "await_expression") == 0) {
        result = dart_unwrap_future(dart_eval_chain(ctx, node));
        goto done;
    }
    if (strcmp(kind, "type_cast_expression") == 0) {
        result = dart_eval_chain(ctx, node);
        TSNode cast = dart_named_child_kind(node, "type_cast");
        TSNode type = dart_find_kind(cast, "type_identifier", 3);
        if (!ts_node_is_null(type)) {
            result = dart_parse_type_node(ctx, type);
        }
        goto done;
    }
    if (strcmp(kind, "assignment_expression") == 0) {
        TSNode right = ts_node_child_by_field_name(node, TS_FIELD("right"));
        result = dart_eval_expr_type(ctx, right);
        goto done;
    }
    if (strcmp(kind, "type_test_expression") == 0 || strcmp(kind, "equality_expression") == 0 ||
        strcmp(kind, "relational_expression") == 0 || strcmp(kind, "logical_or_expression") == 0 ||
        strcmp(kind, "logical_and_expression") == 0) {
        uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            (void)dart_eval_expr_type(ctx, ts_node_named_child(node, i));
        }
        result = cbm_type_named(ctx->arena, "dart.core.bool");
        goto done;
    }
    if (strcmp(kind, "conditional_expression") == 0 || strcmp(kind, "if_null_expression") == 0) {
        uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const CBMType *candidate = dart_eval_expr_type(ctx, ts_node_named_child(node, i));
            if (!cbm_type_is_unknown(candidate)) {
                result = candidate;
            }
        }
        goto done;
    }
    if (strcmp(kind, "throw_expression") == 0) {
        result = cbm_type_named(ctx->arena, "dart.core.Never");
        goto done;
    }

    if (dart_has_direct_chain(node) || strcmp(kind, "initialized_variable_definition") == 0 ||
        strcmp(kind, "initialized_identifier") == 0 ||
        strcmp(kind, "static_final_declaration") == 0 || strcmp(kind, "argument") == 0 ||
        strcmp(kind, "function_body") == 0 || strcmp(kind, "expression_statement") == 0 ||
        strcmp(kind, "new_expression") == 0 || strcmp(kind, "const_object_expression") == 0 ||
        strcmp(kind, "parenthesized_expression") == 0) {
        result = dart_eval_chain(ctx, node);
        goto done;
    }

    {
        uint32_t count = ts_node_named_child_count(node);
        if (count == 1) {
            result = dart_eval_expr_type(ctx, ts_node_named_child(node, 0));
        } else if (count > 1 && (strcmp(kind, "unary_expression") == 0 ||
                                 strcmp(kind, "additive_expression") == 0 ||
                                 strcmp(kind, "multiplicative_expression") == 0 ||
                                 strcmp(kind, "shift_expression") == 0)) {
            result = dart_eval_expr_type(ctx, ts_node_named_child(node, 0));
            for (uint32_t i = 1; i < count; i++) {
                (void)dart_eval_expr_type(ctx, ts_node_named_child(node, i));
            }
        }
    }

done:
    ctx->eval_depth--;
    return result ? result : cbm_type_unknown();
}

static void dart_bind_param_node(DartLSPContext *ctx, TSNode parameter) {
    TSNode name = ts_node_child_by_field_name(parameter, TS_FIELD("name"));
    if (ts_node_is_null(name)) {
        name = dart_find_kind(parameter, "identifier", 4);
    }
    if (ts_node_is_null(name)) {
        return;
    }
    const char *param_name = dart_node_text(ctx, name);
    const CBMType *type = dart_decl_type(ctx, parameter);
    if (cbm_type_is_unknown(type) && ctx->enclosing_class_qn) {
        type = dart_lookup_property_type(ctx, ctx->enclosing_class_qn, param_name);
    }
    cbm_scope_bind(ctx->current_scope, param_name, type);
}

static void dart_bind_params(DartLSPContext *ctx, TSNode signature) {
    TSNode params = dart_find_kind(signature, "formal_parameter_list", 5);
    if (ts_node_is_null(params)) {
        return;
    }
    uint32_t count = ts_node_named_child_count(params);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(params, i);
        if (dart_is(child, "formal_parameter")) {
            dart_bind_param_node(ctx, child);
        } else {
            uint32_t nested_count = ts_node_named_child_count(child);
            for (uint32_t j = 0; j < nested_count; j++) {
                TSNode nested = ts_node_named_child(child, j);
                if (dart_is(nested, "formal_parameter")) {
                    dart_bind_param_node(ctx, nested);
                }
            }
        }
    }
}

static void dart_bind_class_fields_depth(DartLSPContext *ctx, const char *class_qn, int depth,
                                         bool include_instance) {
    if (!class_qn || depth >= CBM_LSP_MAX_LOOKUP_DEPTH) {
        return;
    }
    const CBMRegisteredType *type = cbm_registry_lookup_type(ctx->registry, class_qn);
    if (!type) {
        return;
    }
    if (type->embedded_types) {
        for (int i = 0; type->embedded_types[i]; i++) {
            dart_bind_class_fields_depth(ctx, type->embedded_types[i], depth + 1, include_instance);
        }
    }
    if (type->field_names && type->field_types) {
        for (int i = 0; type->field_names[i]; i++) {
            const CBMDartFieldInfo *field =
                dart_find_direct_field(ctx, class_qn, type->field_names[i]);
            bool is_static = field && field->is_static;
            if ((is_static && depth == 0) || (!is_static && include_instance)) {
                cbm_scope_bind(ctx->current_scope, type->field_names[i], type->field_types[i]);
            }
        }
    }
}

static void dart_bind_class_fields(DartLSPContext *ctx, const char *class_qn,
                                   bool include_instance) {
    dart_bind_class_fields_depth(ctx, class_qn, 0, include_instance);
}

static void dart_process_variable_definition(DartLSPContext *ctx, TSNode definition) {
    TSNode name = ts_node_child_by_field_name(definition, TS_FIELD("name"));
    if (ts_node_is_null(name)) {
        name = dart_named_child_kind(definition, "identifier");
    }
    if (ts_node_is_null(name)) {
        return;
    }
    const CBMType *type = dart_decl_type(ctx, definition);
    const CBMType *inferred = dart_eval_chain(ctx, definition);
    if (cbm_type_is_unknown(type)) {
        type = inferred;
    }
    cbm_scope_bind(ctx->current_scope, dart_node_text(ctx, name), type);
}

static void dart_process_variable_decl(DartLSPContext *ctx, TSNode node) {
    if (dart_is(node, "initialized_variable_definition") ||
        dart_is(node, "initialized_identifier")) {
        dart_process_variable_definition(ctx, node);
        return;
    }
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *kind = ts_node_type(child);
        if (strcmp(kind, "initialized_variable_definition") == 0 ||
            strcmp(kind, "initialized_identifier") == 0) {
            dart_process_variable_definition(ctx, child);
        } else if (strcmp(kind, "initialized_identifier_list") == 0) {
            dart_process_variable_decl(ctx, child);
        }
    }
}

static bool dart_promotion(DartLSPContext *ctx, TSNode condition, const char **name_out,
                           const CBMType **type_out) {
    TSNode test = dart_is(condition, "type_test_expression")
                      ? condition
                      : dart_find_kind(condition, "type_test_expression", 4);
    if (ts_node_is_null(test)) {
        return false;
    }
    TSNode name = dart_named_child_kind(test, "identifier");
    TSNode type_test = dart_named_child_kind(test, "type_test");
    TSNode type = dart_find_kind(type_test, "type_identifier", 3);
    if (ts_node_is_null(name) || ts_node_is_null(type)) {
        return false;
    }
    char *test_text = dart_node_text(ctx, type_test);
    if (test_text && strstr(test_text, "is!") != NULL) {
        return false;
    }
    const CBMType *resolved = dart_parse_type_node(ctx, type);
    if (cbm_type_is_unknown(resolved)) {
        return false;
    }
    *name_out = dart_node_text(ctx, name);
    *type_out = resolved;
    return true;
}

static void dart_process_if(DartLSPContext *ctx, TSNode node) {
    TSNode consequence = ts_node_child_by_field_name(node, TS_FIELD("consequence"));
    TSNode alternative = ts_node_child_by_field_name(node, TS_FIELD("alternative"));
    TSNode condition = {0};
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!ts_node_eq(child, consequence) && !ts_node_eq(child, alternative)) {
            condition = child;
            break;
        }
    }
    (void)dart_eval_expr_type(ctx, condition);

    const char *promoted_name = NULL;
    const CBMType *promoted_type = NULL;
    if (!ts_node_is_null(consequence)) {
        CBMScope *saved = ctx->current_scope;
        ctx->current_scope = cbm_scope_push(ctx->arena, saved);
        if (dart_promotion(ctx, condition, &promoted_name, &promoted_type)) {
            cbm_scope_bind(ctx->current_scope, promoted_name, promoted_type);
        }
        if (dart_is(consequence, "block")) {
            dart_process_block(ctx, consequence, false);
        } else {
            dart_walk_node(ctx, consequence);
        }
        ctx->current_scope = saved;
    }
    if (!ts_node_is_null(alternative)) {
        dart_walk_node(ctx, alternative);
    }
}

static void dart_process_block(DartLSPContext *ctx, TSNode block, bool push_scope) {
    CBMScope *saved = ctx->current_scope;
    if (push_scope) {
        ctx->current_scope = cbm_scope_push(ctx->arena, saved);
    }
    uint32_t count = ts_node_named_child_count(block);
    for (uint32_t i = 0; i < count; i++) {
        dart_walk_node(ctx, ts_node_named_child(block, i));
    }
    if (push_scope) {
        ctx->current_scope = saved;
    }
}

static void dart_walk_node_inner(DartLSPContext *ctx, TSNode node) {
    const char *kind = ts_node_type(node);
    if (strcmp(kind, "block") == 0) {
        dart_process_block(ctx, node, true);
        return;
    }
    if (strcmp(kind, "local_variable_declaration") == 0) {
        dart_process_variable_decl(ctx, node);
        return;
    }
    if (strcmp(kind, "if_statement") == 0) {
        dart_process_if(ctx, node);
        return;
    }
    if (strcmp(kind, "expression_statement") == 0 || strcmp(kind, "return_statement") == 0 ||
        strcmp(kind, "yield_statement") == 0 || strcmp(kind, "assert_statement") == 0) {
        (void)dart_eval_expr_type(ctx, node);
        return;
    }
    if (strcmp(kind, "function_body") == 0) {
        TSNode block = dart_named_child_kind(node, "block");
        if (!ts_node_is_null(block)) {
            dart_process_block(ctx, block, false);
        } else {
            (void)dart_eval_expr_type(ctx, node);
        }
        return;
    }
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *child_kind = ts_node_type(child);
        if (strcmp(child_kind, "selector") == 0 || strcmp(child_kind, "cascade_section") == 0 ||
            strcmp(child_kind, "method_signature") == 0 ||
            strcmp(child_kind, "function_signature") == 0) {
            continue;
        }
        dart_walk_node(ctx, child);
    }
}

static void dart_walk_node(DartLSPContext *ctx, TSNode node) {
    if (!ctx || ts_node_is_null(node) || ctx->walk_depth >= cbm_lsp_max_walk_depth()) {
        return;
    }
    ctx->walk_depth++;
    dart_walk_node_inner(ctx, node);
    ctx->walk_depth--;
}

static const char *dart_first_super(DartLSPContext *ctx, const char *class_qn) {
    const CBMRegisteredType *type = cbm_registry_lookup_type(ctx->registry, class_qn);
    return type && type->embedded_types ? type->embedded_types[0] : NULL;
}

static void dart_process_callable(DartLSPContext *ctx, TSNode signature, TSNode body,
                                  const char *receiver_qn, const char *caller_qn) {
    if (ts_node_is_null(signature) || ts_node_is_null(body) || !caller_qn) {
        return;
    }
    const char *saved_func = ctx->enclosing_func_qn;
    const char *saved_class = ctx->enclosing_class_qn;
    const char *saved_super_qn = ctx->enclosing_super_qn;
    const CBMType *saved_this = ctx->this_type;
    const CBMType *saved_super = ctx->super_type;
    CBMScope *saved_scope = ctx->current_scope;

    ctx->enclosing_func_qn = caller_qn;
    ctx->enclosing_class_qn = receiver_qn;
    ctx->enclosing_super_qn = receiver_qn ? dart_first_super(ctx, receiver_qn) : NULL;
    bool static_context =
        dart_has_token(signature, "static", 3) || dart_has_token(signature, "factory", 3);
    ctx->this_type =
        receiver_qn && !static_context ? cbm_type_named(ctx->arena, receiver_qn) : NULL;
    ctx->super_type = ctx->enclosing_super_qn && !static_context
                          ? cbm_type_named(ctx->arena, ctx->enclosing_super_qn)
                          : NULL;
    ctx->current_scope = cbm_scope_push(ctx->arena, saved_scope);
    if (receiver_qn) {
        dart_bind_class_fields(ctx, receiver_qn, !static_context);
    }
    dart_bind_params(ctx, signature);
    dart_walk_node(ctx, body);

    ctx->current_scope = saved_scope;
    ctx->enclosing_func_qn = saved_func;
    ctx->enclosing_class_qn = saved_class;
    ctx->enclosing_super_qn = saved_super_qn;
    ctx->this_type = saved_this;
    ctx->super_type = saved_super;
}

static TSNode dart_following_body(TSNode container, uint32_t index) {
    uint32_t count = ts_node_named_child_count(container);
    for (uint32_t i = index + 1; i < count; i++) {
        TSNode child = ts_node_named_child(container, i);
        if (dart_is(child, "function_body")) {
            return child;
        }
        if (!dart_is(child, "metadata")) {
            break;
        }
    }
    TSNode none = {0};
    return none;
}

static const char *dart_constructor_caller(DartLSPContext *ctx, const char *class_qn,
                                           const char *class_name, TSNode signature) {
    const char *name = dart_constructor_name(ctx, signature, class_name);
    return name ? dart_join(ctx->arena, class_qn, name) : class_qn;
}

static void dart_process_class(DartLSPContext *ctx, TSNode node) {
    const char *class_name = dart_type_decl_name(ctx, node);
    if (!class_name) {
        return;
    }
    const char *class_qn = dart_join(ctx->arena, ctx->module_qn, class_name);
    TSNode body = dart_type_body(node);
    if (ts_node_is_null(body)) {
        return;
    }
    uint32_t count = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < count; i++) {
        TSNode member = ts_node_named_child(body, i);
        if (dart_is(member, "method_signature")) {
            TSNode next_body = dart_following_body(body, i);
            if (ts_node_is_null(next_body)) {
                continue;
            }
            TSNode factory = dart_find_kind(member, "factory_constructor_signature", 3);
            if (!ts_node_is_null(factory)) {
                const char *caller = dart_constructor_caller(ctx, class_qn, class_name, factory);
                dart_process_callable(ctx, factory, next_body, class_qn, caller);
            } else {
                const char *name = dart_decl_name(ctx, member);
                if (name) {
                    dart_process_callable(ctx, member, next_body, class_qn,
                                          dart_join(ctx->arena, class_qn, name));
                }
            }
            continue;
        }
        if (!dart_is(member, "declaration")) {
            continue;
        }
        TSNode ctor = dart_find_kind(member, "constructor_signature", 3);
        if (ts_node_is_null(ctor)) {
            ctor = dart_find_kind(member, "constant_constructor_signature", 3);
        }
        if (ts_node_is_null(ctor)) {
            continue;
        }
        TSNode ctor_body = dart_find_kind(member, "function_body", 3);
        if (ts_node_is_null(ctor_body)) {
            ctor_body = dart_following_body(body, i);
        }
        if (!ts_node_is_null(ctor_body)) {
            const char *caller = dart_constructor_caller(ctx, class_qn, class_name, ctor);
            dart_process_callable(ctx, ctor, ctor_body, class_qn, caller);
        }
    }
}

static void dart_process_extension(DartLSPContext *ctx, TSNode node) {
    TSNode receiver_node = ts_node_child_by_field_name(node, TS_FIELD("class"));
    const CBMType *receiver = dart_parse_type_node(ctx, receiver_node);
    const char *receiver_qn = dart_type_qn(receiver);
    if (!receiver_qn) {
        return;
    }
    const char *extension_name = dart_decl_name(ctx, node);
    if (!extension_name) {
        extension_name = "extension";
    }
    const char *extension_qn = dart_join(ctx->arena, ctx->module_qn, extension_name);
    TSNode body = ts_node_child_by_field_name(node, TS_FIELD("body"));
    if (ts_node_is_null(body)) {
        body = dart_named_child_kind(node, "extension_body");
    }
    uint32_t count = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < count; i++) {
        TSNode member = ts_node_named_child(body, i);
        if (!dart_is(member, "method_signature")) {
            continue;
        }
        TSNode next_body = dart_following_body(body, i);
        const char *name = dart_decl_name(ctx, member);
        if (name && !ts_node_is_null(next_body)) {
            dart_process_callable(ctx, member, next_body, receiver_qn,
                                  dart_join(ctx->arena, extension_qn, name));
        }
    }
}

static void dart_bind_top_level_variables(DartLSPContext *ctx, TSNode root) {
    uint32_t count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        TSNode node = ts_node_named_child(root, i);
        if (!dart_is(node, "declaration")) {
            continue;
        }
        const CBMType *declared = dart_decl_type(ctx, node);
        const char *names[64];
        const CBMType *types[64];
        int field_count = dart_collect_decl_names(ctx, node, names, types, 0, 64, declared);
        for (int j = 0; j < field_count; j++) {
            cbm_scope_bind(ctx->current_scope, names[j], types[j]);
        }
    }
}

static void dart_resolve_top_level(DartLSPContext *ctx, TSNode root) {
    uint32_t count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        TSNode node = ts_node_named_child(root, i);
        const char *kind = ts_node_type(node);
        if (strcmp(kind, "function_signature") == 0 || strcmp(kind, "getter_signature") == 0 ||
            strcmp(kind, "setter_signature") == 0) {
            const char *name = dart_decl_name(ctx, node);
            TSNode body = dart_following_body(root, i);
            if (name && !ts_node_is_null(body)) {
                dart_process_callable(ctx, node, body, NULL,
                                      dart_join(ctx->arena, ctx->module_qn, name));
            }
        } else if (dart_type_decl(node)) {
            dart_process_class(ctx, node);
        } else if (strcmp(kind, "extension_declaration") == 0) {
            dart_process_extension(ctx, node);
        }
    }
}

void dart_lsp_process_file(DartLSPContext *ctx, TSNode root) {
    if (!ctx || ts_node_is_null(root)) {
        return;
    }
    dart_build_registry(ctx, root);
    dart_bind_top_level_variables(ctx, root);
    dart_resolve_top_level(ctx, root);
}

void cbm_run_dart_lsp(CBMArena *arena, CBMFileResult *result, const char *source, int source_len,
                      TSNode root) {
    if (!arena || !result || !source || ts_node_is_null(root)) {
        return;
    }
    CBMTypeRegistry registry;
    cbm_registry_init(&registry, arena);
    dart_seed_core(&registry, arena);

    const char *module_qn = result->module_qn ? result->module_qn : "";
    const char *project_name = module_qn;
    const char *dot = strchr(module_qn, '.');
    if (dot) {
        size_t len = (size_t)(dot - module_qn);
        char *copy = (char *)cbm_arena_alloc(arena, len + 1);
        if (copy) {
            memcpy(copy, module_qn, len);
            copy[len] = '\0';
            project_name = copy;
        }
    }

    DartLSPContext ctx;
    dart_lsp_init(&ctx, arena, source, source_len, &registry, module_qn, project_name, NULL,
                  &result->resolved_calls);
    dart_lsp_process_file(&ctx, root);
}

#undef TS_FIELD
