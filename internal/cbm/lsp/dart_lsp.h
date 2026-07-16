/*
 * dart_lsp.h - Dart light semantic pass.
 *
 * The pass is intentionally small and conservative.  It resolves calls only
 * when a receiver or top-level declaration can be typed from the current
 * file, an import, or the inline dart:core seed.  Anything else is left for
 * the existing name-based resolver.
 */
#ifndef CBM_LSP_DART_LSP_H
#define CBM_LSP_DART_LSP_H

#include "../cbm.h"
#include "go_lsp.h" /* CBMLSPDef / CBMResolvedCallArray */
#include "scope.h"
#include "type_registry.h"

typedef struct {
    const char *uri;
    const char *module_qn;
    const char *prefix;
    const char **show_names;
    int show_count;
    const char **hide_names;
    int hide_count;
} CBMDartImport;

typedef struct {
    const char *owner_qn;
    const char *name;
    const CBMType *type;
    bool is_static;
} CBMDartFieldInfo;

typedef struct DartLSPContext {
    CBMArena *arena;
    const char *source;
    int source_len;
    const CBMTypeRegistry *registry;
    CBMScope *current_scope;

    const char *module_qn;
    const char *project_name;
    const char *rel_path;

    CBMDartImport *imports;
    int import_count;
    int import_cap;

    CBMDartFieldInfo *fields;
    int field_count;
    int field_cap;

    const char *enclosing_class_qn;
    const char *enclosing_func_qn;
    const char *enclosing_super_qn;
    const CBMType *this_type;
    const CBMType *super_type;

    CBMResolvedCallArray *resolved_calls;
    int eval_depth;
    int walk_depth;
    bool debug;
} DartLSPContext;

void dart_lsp_init(DartLSPContext *ctx, CBMArena *arena, const char *source, int source_len,
                   const CBMTypeRegistry *registry, const char *module_qn, const char *project_name,
                   const char *rel_path, CBMResolvedCallArray *out);

void dart_lsp_process_file(DartLSPContext *ctx, TSNode root);

const CBMType *dart_eval_expr_type(DartLSPContext *ctx, TSNode node);
const CBMType *dart_parse_type_node(DartLSPContext *ctx, TSNode node);

const char *dart_resolve_class_name(DartLSPContext *ctx, const char *name);
const char *dart_resolve_function_name(DartLSPContext *ctx, const char *name);

const CBMRegisteredFunc *dart_lookup_method(DartLSPContext *ctx, const char *class_qn,
                                            const char *method_name);
const CBMType *dart_lookup_property_type(DartLSPContext *ctx, const char *class_qn,
                                         const char *property_name);

void cbm_run_dart_lsp(CBMArena *arena, CBMFileResult *result, const char *source, int source_len,
                      TSNode root);

#endif /* CBM_LSP_DART_LSP_H */
