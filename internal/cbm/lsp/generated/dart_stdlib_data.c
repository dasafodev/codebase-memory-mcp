/*
 * dart_stdlib_data.c - Curated Dart standard-library registry data.
 *
 * The entries below are hand-transcribed API shapes from the public Dart API
 * documentation. They intentionally cover common, stable APIs only: an absent
 * edge is preferable to attributing a call to the wrong declaration.
 *
 * This file is included from lsp_all.c.
 */

#include "../dart_lsp.h"

#include <string.h>

static const char *const DART_DEFAULT_IMPORTS[] = {"dart.core"};

const char *const *cbm_dart_default_import_packages(int *count_out) {
    if (count_out) {
        *count_out = (int)(sizeof(DART_DEFAULT_IMPORTS) / sizeof(DART_DEFAULT_IMPORTS[0]));
    }
    return DART_DEFAULT_IMPORTS;
}

static const CBMType *dart_stdlib_signature(CBMArena *arena, const CBMType *return_type) {
    const CBMType *returns[] = {return_type ? return_type : cbm_type_unknown(), NULL};
    return cbm_type_func(arena, NULL, NULL, returns);
}

static const CBMType *dart_stdlib_t1(CBMArena *arena, const char *qn, const CBMType *arg) {
    const CBMType *args[] = {arg};
    return cbm_type_template(arena, qn, args, 1);
}

static const CBMType *dart_stdlib_t2(CBMArena *arena, const char *qn, const CBMType *first,
                                     const CBMType *second) {
    const CBMType *args[] = {first, second};
    return cbm_type_template(arena, qn, args, 2);
}

static void dart_stdlib_add_type(CBMTypeRegistry *registry, CBMArena *arena, const char *qn,
                                 const char *parent, const char *first_param,
                                 const char *second_param, bool is_interface) {
    CBMRegisteredType type = {0};
    type.qualified_name = qn;
    type.short_name = strrchr(qn, '.') ? strrchr(qn, '.') + 1 : qn;
    type.is_interface = is_interface;
    if (parent) {
        const char **parents = cbm_arena_alloc(arena, 2 * sizeof(*parents));
        parents[0] = parent;
        parents[1] = NULL;
        type.embedded_types = parents;
    }
    if (first_param) {
        const char **params = cbm_arena_alloc(arena, 3 * sizeof(*params));
        params[0] = first_param;
        params[1] = second_param;
        params[2] = NULL;
        type.type_param_names = params;
    }
    cbm_registry_add_type(registry, type);
}

static void dart_stdlib_add_method(CBMTypeRegistry *registry, CBMArena *arena,
                                   const char *receiver, const char *name,
                                   const CBMType *return_type, int flags,
                                   const char *type_param) {
    CBMRegisteredFunc func = {0};
    func.receiver_type = receiver;
    func.short_name = name;
    func.qualified_name = cbm_arena_sprintf(arena, "%s.%s", receiver, name);
    func.signature = dart_stdlib_signature(arena, return_type);
    func.min_params = 0;
    func.flags = flags;
    if (type_param) {
        const char **params = cbm_arena_alloc(arena, 2 * sizeof(*params));
        params[0] = type_param;
        params[1] = NULL;
        func.type_param_names = params;
    }
    cbm_registry_add_func(registry, func);
}

static void dart_stdlib_add_function(CBMTypeRegistry *registry, CBMArena *arena,
                                     const char *library, const char *name,
                                     const CBMType *return_type, int flags,
                                     const char *type_param) {
    CBMRegisteredFunc func = {0};
    func.short_name = name;
    func.qualified_name = cbm_arena_sprintf(arena, "%s.%s", library, name);
    func.signature = dart_stdlib_signature(arena, return_type);
    func.min_params = 0;
    func.flags = flags;
    if (type_param) {
        const char **params = cbm_arena_alloc(arena, 2 * sizeof(*params));
        params[0] = type_param;
        params[1] = NULL;
        func.type_param_names = params;
    }
    cbm_registry_add_func(registry, func);
}

static void dart_stdlib_add_constructor(CBMTypeRegistry *registry, CBMArena *arena,
                                        const char *type_qn, const char *name,
                                        const CBMType *return_type, bool factory) {
    CBMRegisteredFunc func = {0};
    func.receiver_type = type_qn;
    func.short_name = name ? name : "<init>";
    func.qualified_name = name ? cbm_arena_sprintf(arena, "%s.%s", type_qn, name) : type_qn;
    func.signature = dart_stdlib_signature(arena, return_type);
    func.min_params = 0;
    func.flags = CBM_FUNC_FLAG_STATICMETHOD | CBM_DART_FUNC_FLAG_CONSTRUCTOR;
    if (factory) {
        func.flags |= CBM_DART_FUNC_FLAG_FACTORY;
    }
    cbm_registry_add_func(registry, func);
}

void cbm_dart_stdlib_register(CBMTypeRegistry *registry, CBMArena *arena) {
    const CBMType *object_t = cbm_type_named(arena, "dart.core.Object");
    const CBMType *void_t = cbm_type_named(arena, "dart.core.void");
    const CBMType *bool_t = cbm_type_named(arena, "dart.core.bool");
    const CBMType *num_t = cbm_type_named(arena, "dart.core.num");
    const CBMType *int_t = cbm_type_named(arena, "dart.core.int");
    const CBMType *double_t = cbm_type_named(arena, "dart.core.double");
    const CBMType *string_t = cbm_type_named(arena, "dart.core.String");
    const CBMType *duration_t = cbm_type_named(arena, "dart.core.Duration");
    const CBMType *date_time_t = cbm_type_named(arena, "dart.core.DateTime");
    const CBMType *uri_t = cbm_type_named(arena, "dart.core.Uri");
    const CBMType *regexp_t = cbm_type_named(arena, "dart.core.RegExp");
    const CBMType *match_t = cbm_type_named(arena, "dart.core.Match");
    const CBMType *stack_trace_t = cbm_type_named(arena, "dart.core.StackTrace");
    const CBMType *dynamic_t = cbm_type_unknown();
    const CBMType *e_t = cbm_type_type_param(arena, "E");
    const CBMType *k_t = cbm_type_type_param(arena, "K");
    const CBMType *v_t = cbm_type_type_param(arena, "V");
    const CBMType *r_t = cbm_type_type_param(arena, "R");
    const CBMType *t_t = cbm_type_type_param(arena, "T");

    /* dart:core */
    dart_stdlib_add_type(registry, arena, "dart.core.Object", NULL, NULL, NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.core.Function", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Type", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Symbol", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Null", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Never", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.bool", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.num", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.int", "dart.core.num", NULL, NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.core.double", "dart.core.num", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.BigInt", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Pattern", "dart.core.Object", NULL, NULL,
                         true);
    dart_stdlib_add_type(registry, arena, "dart.core.Match", "dart.core.Object", NULL, NULL,
                         true);
    dart_stdlib_add_type(registry, arena, "dart.core.String", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.StringBuffer", "dart.core.Object", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.core.RegExp", "dart.core.Pattern", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Runes", "dart.core.Iterable", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Iterable", "dart.core.Object", "E", NULL,
                         true);
    dart_stdlib_add_type(registry, arena, "dart.core.Iterator", "dart.core.Object", "E", NULL,
                         true);
    dart_stdlib_add_type(registry, arena, "dart.core.List", "dart.core.Iterable", "E", NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Set", "dart.core.Iterable", "E", NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Map", "dart.core.Object", "K", "V",
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.MapEntry", "dart.core.Object", "K", "V",
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Enum", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Uri", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.DateTime", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Duration", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Stopwatch", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.StackTrace", "dart.core.Object", NULL, NULL,
                         true);
    dart_stdlib_add_type(registry, arena, "dart.core.Error", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.Exception", "dart.core.Object", NULL, NULL,
                         true);
    dart_stdlib_add_type(registry, arena, "dart.core.ArgumentError", "dart.core.Error", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.core.StateError", "dart.core.Error", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.core.RangeError", "dart.core.ArgumentError", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.core.FormatException", "dart.core.Object", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.core.UnsupportedError", "dart.core.Error", NULL,
                         NULL, false);

    dart_stdlib_add_method(registry, arena, "dart.core.Object", "toString", string_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Object", "hashCode", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Object", "runtimeType",
                           cbm_type_named(arena, "dart.core.Type"), CBM_FUNC_FLAG_PROPERTY, NULL);

    static const char *const string_to_string[] = {
        "toUpperCase", "toLowerCase", "trim",       "trimLeft", "trimRight",
        "substring",   "replaceAll",  "replaceFirst", "padLeft",  "padRight",
        NULL};
    for (int i = 0; string_to_string[i]; i++) {
        dart_stdlib_add_method(registry, arena, "dart.core.String", string_to_string[i], string_t,
                               0, NULL);
    }
    static const char *const string_to_bool[] = {"contains", "startsWith", "endsWith", "isEmpty",
                                                 "isNotEmpty", NULL};
    for (int i = 0; string_to_bool[i]; i++) {
        int flags = (strstr(string_to_bool[i], "Empty") != NULL) ? CBM_FUNC_FLAG_PROPERTY : 0;
        dart_stdlib_add_method(registry, arena, "dart.core.String", string_to_bool[i], bool_t,
                               flags, NULL);
    }
    dart_stdlib_add_method(registry, arena, "dart.core.String", "split",
                           dart_stdlib_t1(arena, "dart.core.List", string_t), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.String", "length", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.String", "codeUnits",
                           dart_stdlib_t1(arena, "dart.core.List", int_t), CBM_FUNC_FLAG_PROPERTY,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.String", "runes",
                           cbm_type_named(arena, "dart.core.Runes"), CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.String", "indexOf", int_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.String", "lastIndexOf", int_t, 0, NULL);

    dart_stdlib_add_method(registry, arena, "dart.core.num", "abs", num_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.num", "ceil", int_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.num", "floor", int_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.num", "round", int_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.num", "truncate", int_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.num", "toDouble", double_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.num", "toInt", int_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.num", "isNaN", bool_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.num", "isInfinite", bool_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);

    const CBMType *iter_e = dart_stdlib_t1(arena, "dart.core.Iterable", e_t);
    const CBMType *iter_r = dart_stdlib_t1(arena, "dart.core.Iterable", r_t);
    const CBMType *list_e = dart_stdlib_t1(arena, "dart.core.List", e_t);
    const CBMType *set_e = dart_stdlib_t1(arena, "dart.core.Set", e_t);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "map", iter_r, 0, "R");
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "expand", iter_r, 0, "R");
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "where", iter_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "followedBy", iter_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "skip", iter_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "skipWhile", iter_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "take", iter_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "takeWhile", iter_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "toList", list_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "toSet", set_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "first", e_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "last", e_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "single", e_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "length", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "isEmpty", bool_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "isNotEmpty", bool_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "contains", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "any", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "every", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "join", string_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterable", "iterator",
                           dart_stdlib_t1(arena, "dart.core.Iterator", e_t),
                           CBM_FUNC_FLAG_PROPERTY, NULL);

    dart_stdlib_add_method(registry, arena, "dart.core.Iterator", "current", e_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Iterator", "moveNext", bool_t, 0, NULL);

    dart_stdlib_add_method(registry, arena, "dart.core.List", "add", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "addAll", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "clear", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "sort", void_t, 0, NULL);
    /* Keep the receiver-specific attribution used by the Phase 1 pass. */
    dart_stdlib_add_method(registry, arena, "dart.core.List", "map", iter_r, 0, "R");
    dart_stdlib_add_method(registry, arena, "dart.core.List", "where", iter_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "remove", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "removeAt", e_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "removeLast", e_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "sublist", list_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "reversed", iter_e,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "length", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.List", "first", e_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);

    dart_stdlib_add_method(registry, arena, "dart.core.Set", "add", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Set", "addAll", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Set", "remove", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Set", "contains", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Set", "union", set_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Set", "intersection", set_e, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Set", "difference", set_e, 0, NULL);

    const CBMType *iter_k = dart_stdlib_t1(arena, "dart.core.Iterable", k_t);
    const CBMType *iter_v = dart_stdlib_t1(arena, "dart.core.Iterable", v_t);
    const CBMType *entry_kv = dart_stdlib_t2(arena, "dart.core.MapEntry", k_t, v_t);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "keys", iter_k,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "values", iter_v,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "entries",
                           dart_stdlib_t1(arena, "dart.core.Iterable", entry_kv),
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "length", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "isEmpty", bool_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "containsKey", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "containsValue", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "remove", v_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "putIfAbsent", v_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Map", "update", v_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.MapEntry", "key", k_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.MapEntry", "value", v_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);

    dart_stdlib_add_method(registry, arena, "dart.core.Uri", "parse", uri_t,
                           CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Uri", "tryParse", uri_t,
                           CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Uri", "resolve", uri_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Uri", "host", string_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Uri", "path", string_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Uri", "query", string_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);

    dart_stdlib_add_method(registry, arena, "dart.core.DateTime", "now", date_time_t,
                           CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.DateTime", "parse", date_time_t,
                           CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.DateTime", "tryParse", date_time_t,
                           CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.DateTime", "add", date_time_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.DateTime", "subtract", date_time_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.DateTime", "difference", duration_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.DateTime", "millisecondsSinceEpoch", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Duration", "inDays", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Duration", "inHours", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Duration", "inMinutes", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Duration", "inSeconds", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Duration", "inMilliseconds", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.RegExp", "hasMatch", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.RegExp", "firstMatch", match_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.RegExp", "allMatches",
                           dart_stdlib_t1(arena, "dart.core.Iterable", match_t), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Match", "group", string_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Match", "start", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Match", "end", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.StringBuffer", "write", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.StringBuffer", "writeln", void_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.StringBuffer", "clear", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Stopwatch", "start", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Stopwatch", "stop", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Stopwatch", "reset", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.core.Stopwatch", "elapsed", duration_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);

    dart_stdlib_add_constructor(registry, arena, "dart.core.Object", NULL, object_t, false);
    dart_stdlib_add_constructor(registry, arena, "dart.core.StringBuffer", NULL,
                                cbm_type_named(arena, "dart.core.StringBuffer"), false);
    dart_stdlib_add_constructor(registry, arena, "dart.core.List", NULL, list_e, true);
    dart_stdlib_add_constructor(registry, arena, "dart.core.Set", NULL, set_e, true);
    dart_stdlib_add_constructor(registry, arena, "dart.core.Map", NULL,
                                dart_stdlib_t2(arena, "dart.core.Map", k_t, v_t), true);
    dart_stdlib_add_constructor(registry, arena, "dart.core.Uri", NULL, uri_t, false);
    dart_stdlib_add_constructor(registry, arena, "dart.core.DateTime", NULL, date_time_t, false);
    dart_stdlib_add_constructor(registry, arena, "dart.core.Duration", NULL, duration_t, false);
    dart_stdlib_add_constructor(registry, arena, "dart.core.RegExp", NULL, regexp_t, true);
    dart_stdlib_add_constructor(registry, arena, "dart.core.Stopwatch", NULL,
                                cbm_type_named(arena, "dart.core.Stopwatch"), false);
    dart_stdlib_add_function(registry, arena, "dart.core", "print", void_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.core", "identical", bool_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.core", "identityHashCode", int_t, 0, NULL);

    /* dart:async */
    dart_stdlib_add_type(registry, arena, "dart.async.Future", "dart.core.Object", "T", NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.async.FutureOr", "dart.core.Object", "T", NULL,
                         true);
    dart_stdlib_add_type(registry, arena, "dart.async.Stream", "dart.core.Object", "T", NULL,
                         true);
    dart_stdlib_add_type(registry, arena, "dart.async.StreamSubscription", "dart.core.Object",
                         "T", NULL, true);
    dart_stdlib_add_type(registry, arena, "dart.async.StreamController", "dart.core.Object", "T",
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.async.EventSink", "dart.core.Object", "T", NULL,
                         true);
    dart_stdlib_add_type(registry, arena, "dart.async.Completer", "dart.core.Object", "T", NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.async.Timer", "dart.core.Object", NULL, NULL,
                         false);

    const CBMType *future_t = dart_stdlib_t1(arena, "dart.async.Future", t_t);
    const CBMType *future_r = dart_stdlib_t1(arena, "dart.async.Future", r_t);
    const CBMType *stream_t = dart_stdlib_t1(arena, "dart.async.Stream", t_t);
    dart_stdlib_add_method(registry, arena, "dart.async.Future", "then", future_r, 0, "R");
    dart_stdlib_add_method(registry, arena, "dart.async.Future", "catchError", future_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Future", "whenComplete", future_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Future", "timeout", future_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Future", "asStream", stream_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Stream", "map",
                           dart_stdlib_t1(arena, "dart.async.Stream", r_t), 0, "R");
    dart_stdlib_add_method(registry, arena, "dart.async.Stream", "where", stream_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Stream", "take", stream_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Stream", "skip", stream_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Stream", "listen",
                           dart_stdlib_t1(arena, "dart.async.StreamSubscription", t_t), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Stream", "toList",
                           dart_stdlib_t1(arena, "dart.async.Future", list_e), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Stream", "first", future_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.StreamSubscription", "cancel",
                           dart_stdlib_t1(arena, "dart.async.Future", void_t), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.StreamSubscription", "pause", void_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.StreamSubscription", "resume", void_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.StreamController", "stream", stream_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.StreamController", "sink",
                           dart_stdlib_t1(arena, "dart.async.EventSink", t_t),
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.StreamController", "add", void_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.StreamController", "close", future_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Completer", "future", future_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Completer", "complete", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Timer", "cancel", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.async.Timer", "isActive", bool_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_constructor(registry, arena, "dart.async.Future", NULL, future_t, false);
    dart_stdlib_add_constructor(registry, arena, "dart.async.StreamController", NULL,
                                cbm_type_named(arena, "dart.async.StreamController"), true);
    dart_stdlib_add_constructor(registry, arena, "dart.async.Completer", NULL,
                                cbm_type_named(arena, "dart.async.Completer"), true);
    dart_stdlib_add_constructor(registry, arena, "dart.async.Timer", NULL,
                                cbm_type_named(arena, "dart.async.Timer"), true);

    /* dart:collection */
    static const struct {
        const char *qn;
        const char *parent;
        const char *p1;
        const char *p2;
    } collection_types[] = {{"dart.collection.HashMap", "dart.core.Map", "K", "V"},
                            {"dart.collection.LinkedHashMap", "dart.core.Map", "K", "V"},
                            {"dart.collection.SplayTreeMap", "dart.core.Map", "K", "V"},
                            {"dart.collection.HashSet", "dart.core.Set", "E", NULL},
                            {"dart.collection.LinkedHashSet", "dart.core.Set", "E", NULL},
                            {"dart.collection.SplayTreeSet", "dart.core.Set", "E", NULL},
                            {"dart.collection.Queue", "dart.core.Iterable", "E", NULL},
                            {"dart.collection.ListQueue", "dart.collection.Queue", "E", NULL},
                            {"dart.collection.DoubleLinkedQueue", "dart.collection.Queue", "E",
                             NULL},
                            {NULL, NULL, NULL, NULL}};
    for (int i = 0; collection_types[i].qn; i++) {
        dart_stdlib_add_type(registry, arena, collection_types[i].qn, collection_types[i].parent,
                             collection_types[i].p1, collection_types[i].p2, false);
        dart_stdlib_add_constructor(registry, arena, collection_types[i].qn, NULL,
                                    cbm_type_named(arena, collection_types[i].qn), true);
    }
    dart_stdlib_add_method(registry, arena, "dart.collection.Queue", "add", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.collection.Queue", "addFirst", void_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.collection.Queue", "addLast", void_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.collection.Queue", "removeFirst", e_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.collection.Queue", "removeLast", e_t, 0, NULL);

    /* dart:convert */
    dart_stdlib_add_type(registry, arena, "dart.convert.Codec", "dart.core.Object", "S", "T",
                         true);
    dart_stdlib_add_type(registry, arena, "dart.convert.Converter", "dart.core.Object", "S", "T",
                         true);
    dart_stdlib_add_type(registry, arena, "dart.convert.Encoding", "dart.convert.Codec", NULL,
                         NULL, true);
    dart_stdlib_add_type(registry, arena, "dart.convert.Utf8Codec", "dart.convert.Encoding", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.convert.JsonCodec", "dart.convert.Codec", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.convert.JsonEncoder", "dart.convert.Converter",
                         NULL, NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.convert.JsonDecoder", "dart.convert.Converter",
                         NULL, NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.convert.Base64Codec", "dart.convert.Codec", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.convert.LineSplitter", "dart.convert.Converter",
                         NULL, NULL, false);
    dart_stdlib_add_method(registry, arena, "dart.convert.Converter", "convert", t_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.convert.Utf8Codec", "encode",
                           dart_stdlib_t1(arena, "dart.core.List", int_t), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.convert.Utf8Codec", "decode", string_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.convert.JsonCodec", "encode", string_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.convert.JsonCodec", "decode", dynamic_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.convert.Base64Codec", "encode", string_t, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.convert.Base64Codec", "decode",
                           cbm_type_named(arena, "dart.typed_data.Uint8List"), 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.convert", "jsonEncode", string_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.convert", "jsonDecode", dynamic_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.convert", "base64Encode", string_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.convert", "base64Decode",
                             cbm_type_named(arena, "dart.typed_data.Uint8List"), 0, NULL);

    /* dart:math */
    dart_stdlib_add_type(registry, arena, "dart.math.Random", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.math.Point", "dart.core.Object", "T", NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.math.Rectangle", "dart.core.Object", "T", NULL,
                         false);
    dart_stdlib_add_method(registry, arena, "dart.math.Random", "nextInt", int_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.math.Random", "nextDouble", double_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.math.Random", "nextBool", bool_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.math.Point", "x", t_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.math.Point", "y", t_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.math.Point", "distanceTo", double_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.math", "min", num_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.math", "max", num_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.math", "sqrt", double_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.math", "pow", num_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.math", "sin", double_t, 0, NULL);
    dart_stdlib_add_function(registry, arena, "dart.math", "cos", double_t, 0, NULL);
    dart_stdlib_add_constructor(registry, arena, "dart.math.Random", NULL,
                                cbm_type_named(arena, "dart.math.Random"), true);
    dart_stdlib_add_constructor(registry, arena, "dart.math.Point", NULL,
                                cbm_type_named(arena, "dart.math.Point"), false);
    dart_stdlib_add_constructor(registry, arena, "dart.math.Rectangle", NULL,
                                cbm_type_named(arena, "dart.math.Rectangle"), false);

    /* dart:io */
    dart_stdlib_add_type(registry, arena, "dart.io.FileSystemEntity", "dart.core.Object", NULL,
                         NULL, true);
    dart_stdlib_add_type(registry, arena, "dart.io.File", "dart.io.FileSystemEntity", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.io.Directory", "dart.io.FileSystemEntity", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.io.Link", "dart.io.FileSystemEntity", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.io.IOSink", "dart.async.EventSink", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.io.RandomAccessFile", "dart.core.Object", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.io.Process", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.io.ProcessResult", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.io.Socket", "dart.async.Stream", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.io.ServerSocket", "dart.async.Stream", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.io.HttpClient", "dart.core.Object", NULL, NULL,
                         false);
    dart_stdlib_add_type(registry, arena, "dart.io.HttpClientRequest", "dart.core.Object", NULL,
                         NULL, false);
    dart_stdlib_add_type(registry, arena, "dart.io.HttpClientResponse", "dart.async.Stream", NULL,
                         NULL, false);
    const CBMType *future_string = dart_stdlib_t1(arena, "dart.async.Future", string_t);
    const CBMType *future_entity = dart_stdlib_t1(
        arena, "dart.async.Future", cbm_type_named(arena, "dart.io.FileSystemEntity"));
    const CBMType *future_file =
        dart_stdlib_t1(arena, "dart.async.Future", cbm_type_named(arena, "dart.io.File"));
    const CBMType *future_directory =
        dart_stdlib_t1(arena, "dart.async.Future", cbm_type_named(arena, "dart.io.Directory"));
    dart_stdlib_add_method(registry, arena, "dart.io.FileSystemEntity", "exists",
                           dart_stdlib_t1(arena, "dart.async.Future", bool_t), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.FileSystemEntity", "delete", future_entity,
                           0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.File", "delete", future_file, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.Directory", "delete", future_directory, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.File", "readAsString", future_string, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.File", "readAsBytes",
                           dart_stdlib_t1(arena, "dart.async.Future",
                                         cbm_type_named(arena, "dart.typed_data.Uint8List")),
                           0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.File", "writeAsString", future_file, 0,
                           NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.File", "openRead",
                           dart_stdlib_t1(arena, "dart.async.Stream",
                                         dart_stdlib_t1(arena, "dart.core.List", int_t)),
                           0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.File", "openWrite",
                           cbm_type_named(arena, "dart.io.IOSink"), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.Directory", "list",
                           dart_stdlib_t1(arena, "dart.async.Stream",
                                         cbm_type_named(arena, "dart.io.FileSystemEntity")),
                           0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.IOSink", "write", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.IOSink", "writeln", void_t, 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.IOSink", "flush",
                           dart_stdlib_t1(arena, "dart.async.Future", void_t), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.IOSink", "close",
                           dart_stdlib_t1(arena, "dart.async.Future", void_t), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.Process", "run",
                           dart_stdlib_t1(arena, "dart.async.Future",
                                         cbm_type_named(arena, "dart.io.ProcessResult")),
                           CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.ProcessResult", "exitCode", int_t,
                           CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.HttpClient", "getUrl",
                           dart_stdlib_t1(arena, "dart.async.Future",
                                         cbm_type_named(arena, "dart.io.HttpClientRequest")),
                           0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.io.HttpClientRequest", "close",
                           dart_stdlib_t1(arena, "dart.async.Future",
                                         cbm_type_named(arena, "dart.io.HttpClientResponse")),
                           0, NULL);
    dart_stdlib_add_constructor(registry, arena, "dart.io.File", NULL,
                                cbm_type_named(arena, "dart.io.File"), false);
    dart_stdlib_add_constructor(registry, arena, "dart.io.Directory", NULL,
                                cbm_type_named(arena, "dart.io.Directory"), false);
    dart_stdlib_add_constructor(registry, arena, "dart.io.HttpClient", NULL,
                                cbm_type_named(arena, "dart.io.HttpClient"), true);

    /* dart:typed_data */
    dart_stdlib_add_type(registry, arena, "dart.typed_data.TypedData", "dart.core.Object", NULL,
                         NULL, true);
    dart_stdlib_add_type(registry, arena, "dart.typed_data.ByteBuffer", "dart.core.Object", NULL,
                         NULL, true);
    dart_stdlib_add_type(registry, arena, "dart.typed_data.ByteData", "dart.typed_data.TypedData",
                         NULL, NULL, false);
    static const char *const typed_lists[] = {
        "Uint8List",  "Uint8ClampedList", "Uint16List", "Uint32List", "Uint64List",
        "Int8List",   "Int16List",        "Int32List",  "Int64List",  "Float32List",
        "Float64List", "Float32x4List",     "Int32x4List", "Float64x2List", NULL};
    for (int i = 0; typed_lists[i]; i++) {
        const char *qn = cbm_arena_sprintf(arena, "dart.typed_data.%s", typed_lists[i]);
        dart_stdlib_add_type(registry, arena, qn, "dart.core.List", NULL, NULL, false);
        dart_stdlib_add_constructor(registry, arena, qn, NULL, cbm_type_named(arena, qn), false);
        dart_stdlib_add_method(registry, arena, qn, "buffer",
                               cbm_type_named(arena, "dart.typed_data.ByteBuffer"),
                               CBM_FUNC_FLAG_PROPERTY, NULL);
    }
    dart_stdlib_add_method(registry, arena, "dart.typed_data.ByteBuffer", "asUint8List",
                           cbm_type_named(arena, "dart.typed_data.Uint8List"), 0, NULL);
    dart_stdlib_add_method(registry, arena, "dart.typed_data.ByteBuffer", "asByteData",
                           cbm_type_named(arena, "dart.typed_data.ByteData"), 0, NULL);

    (void)stack_trace_t;
}
