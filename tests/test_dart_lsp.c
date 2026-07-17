/* Focused tests for the in-process Dart light semantic pass. */
#include "cbm.h"
#include "lsp/dart_lsp.h"
#include "test_framework.h"

#include <string.h>

static CBMFileResult *extract_dart_lsp(const char *source) {
    return cbm_extract_file(source, (int)strlen(source), CBM_LANG_DART, "test", "main.dart", 0,
                            NULL, NULL);
}

static CBMFileResult *extract_dart_lsp_with_external(const char *source, const char *rel_path,
                                                     const char *context_rel_path,
                                                     const char *external_qn) {
    CBMFileResult *result = cbm_extract_file(source, (int)strlen(source), CBM_LANG_DART, "test",
                                             rel_path, 0, NULL, NULL);
    if (!result || !result->cached_tree) {
        return result;
    }

    CBMTypeRegistry registry;
    cbm_registry_init(&registry, &result->arena);
    const CBMType *return_types[] = {cbm_type_builtin(&result->arena, "void"), NULL};
    CBMRegisteredFunc external = {
        .qualified_name = external_qn,
        .short_name = "call",
        .signature = cbm_type_func(&result->arena, NULL, NULL, return_types),
        .min_params = 0,
    };
    cbm_registry_add_func(&registry, external);

    CBMResolvedCallArray resolved = {0};
    DartLSPContext ctx;
    dart_lsp_init(&ctx, &result->arena, source, (int)strlen(source), &registry, result->module_qn,
                  "test", context_rel_path, &resolved);
    dart_lsp_process_file(&ctx, ts_tree_root_node(result->cached_tree));
    result->resolved_calls = resolved;
    return result;
}

static int dart_find_resolved(const CBMFileResult *result, const char *caller, const char *callee) {
    for (int i = 0; i < result->resolved_calls.count; i++) {
        const CBMResolvedCall *call = &result->resolved_calls.items[i];
        if (call->caller_qn && call->callee_qn && strstr(call->caller_qn, caller) &&
            strstr(call->callee_qn, callee)) {
            return i;
        }
    }
    return -1;
}

static int dart_require_resolved(const CBMFileResult *result, const char *caller,
                                 const char *callee) {
    int found = dart_find_resolved(result, caller, callee);
    if (found < 0) {
        printf("  missing Dart resolved call: %s -> %s (have %d)\n", caller, callee,
               result->resolved_calls.count);
        for (int i = 0; i < result->resolved_calls.count; i++) {
            const CBMResolvedCall *call = &result->resolved_calls.items[i];
            printf("    %s -> %s [%s]\n", call->caller_qn ? call->caller_qn : "(null)",
                   call->callee_qn ? call->callee_qn : "(null)",
                   call->strategy ? call->strategy : "(null)");
        }
    }
    return found;
}

static int dart_find_resolved_arr_exact(const CBMResolvedCallArray *calls, const char *caller,
                                        const char *callee) {
    for (int i = 0; i < calls->count; i++) {
        const CBMResolvedCall *call = &calls->items[i];
        if (call->caller_qn && call->callee_qn && strstr(call->caller_qn, caller) &&
            strcmp(call->callee_qn, callee) == 0) {
            return i;
        }
    }
    return -1;
}

static int dart_require_resolved_arr_exact(const CBMResolvedCallArray *calls, const char *caller,
                                           const char *callee) {
    int found = dart_find_resolved_arr_exact(calls, caller, callee);
    if (found < 0) {
        printf("  missing Dart cross-file call: %s -> %s (have %d)\n", caller, callee,
               calls->count);
        for (int i = 0; i < calls->count; i++) {
            const CBMResolvedCall *call = &calls->items[i];
            printf("    %s -> %s [%s]\n", call->caller_qn ? call->caller_qn : "(null)",
                   call->callee_qn ? call->callee_qn : "(null)",
                   call->strategy ? call->strategy : "(null)");
        }
    }
    return found;
}

TEST(dartlsp_core_print) {
    CBMFileResult *r = extract_dart_lsp("void main() { print('hello'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "main", "dart.core.print"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_local_top_level_call) {
    CBMFileResult *r = extract_dart_lsp("String helper() => '';\nvoid main() { helper(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "main", "helper"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_default_constructor) {
    CBMFileResult *r = extract_dart_lsp("class Foo { Foo(); }\nFoo make() => Foo();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "make", "Foo"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_named_constructor) {
    CBMFileResult *r = extract_dart_lsp("class Foo { Foo.named(); }\nFoo make() => Foo.named();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "make", "Foo.named"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_typed_receiver_method) {
    CBMFileResult *r = extract_dart_lsp(
        "class Service { String query() => ''; }\nvoid run(Service s) { s.query(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Service.query"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_inferred_constructor_type) {
    CBMFileResult *r = extract_dart_lsp(
        "class Service { void query() {} }\nvoid run() { final s = Service(); s.query(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Service.query"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_selector_chain_fold) {
    CBMFileResult *r = extract_dart_lsp("class B { String build() => ''; }\n"
                                        "class A { B add() => B(); }\n"
                                        "String run(A a) => a.add().build();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "A.add"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "B.build"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_getter_property) {
    CBMFileResult *r = extract_dart_lsp(
        "class User { String get name => ''; }\nString run(User u) => u.name.trim();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "User.name"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.trim"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_inherited_method) {
    CBMFileResult *r =
        extract_dart_lsp("class Base { void ping() {} }\nclass Child extends Base {}\n"
                         "void run(Child c) { c.ping(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Base.ping"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_cascade_methods) {
    CBMFileResult *r = extract_dart_lsp("class Builder { void add() {} void build() {} }\n"
                                        "void run(Builder b) { b..add()..build(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Builder.add"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Builder.build"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_await_future_unwrap) {
    CBMFileResult *r = extract_dart_lsp(
        "class Value { void use() {} }\nclass Api { Future<Value> load() async => Value(); }\n"
        "Future<void> run(Api api) async { final v = await api.load(); v.use(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Api.load"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Value.use"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_is_promotion) {
    CBMFileResult *r = extract_dart_lsp("class Dog { void bark() {} }\nvoid run(Object value) { if "
                                        "(value is Dog) { value.bark(); } }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Dog.bark"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_string_literal_dispatch) {
    CBMFileResult *r = extract_dart_lsp("String run() => ' x '.trim().toUpperCase();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.trim"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.toUpperCase"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_list_literal_dispatch) {
    CBMFileResult *r = extract_dart_lsp("void run() { final xs = <int>[]; xs.add(1); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "List.add"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_unknown_receiver_abstains) {
    CBMFileResult *r = extract_dart_lsp("void run(dynamic value) { value.missing(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "missing"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_factory_constructor) {
    CBMFileResult *r = extract_dart_lsp(
        "class Foo { Foo._(); factory Foo.make() => Foo._(); }\nFoo run() => Foo.make();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo.make"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_redirecting_factory_constructor) {
    CBMFileResult *r = extract_dart_lsp(
        "class Foo { Foo.named(); factory Foo.make() = Foo.named; }\nFoo run() => Foo.make();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo.make"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_private_named_constructor) {
    CBMFileResult *r = extract_dart_lsp("class Foo { Foo._(); }\nFoo run() => Foo._();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo._"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_static_factory_method) {
    CBMFileResult *r = extract_dart_lsp(
        "class Foo { Foo(); static Foo create() => Foo(); }\nFoo run() => Foo.create();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo.create"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_const_constructor) {
    CBMFileResult *r = extract_dart_lsp("class Foo { const Foo(); }\nFoo run() => const Foo();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_final_typed_local) {
    CBMFileResult *r = extract_dart_lsp("class Foo { void go() {} }\nvoid run(Foo source) { final "
                                        "Foo value = source; value.go(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo.go"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_var_factory_inference) {
    CBMFileResult *r =
        extract_dart_lsp("class Foo { Foo._(); factory Foo.make() => Foo._(); void go() {} }\n"
                         "void run() { var value = Foo.make(); value.go(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo.go"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_const_string_inference) {
    CBMFileResult *r = extract_dart_lsp("void run() { const value = 'x'; value.trim(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.trim"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_declared_field_dispatch) {
    CBMFileResult *r = extract_dart_lsp(
        "class Repo { void query() {} }\nclass Service { final Repo repo; Service(this.repo); "
        "void run() { repo.query(); } }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "Service.run", "Repo.query"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_static_field_chain) {
    CBMFileResult *r =
        extract_dart_lsp("class Foo { Foo(); static final Foo instance = Foo(); void go() {} }\n"
                         "void run() { Foo.instance.go(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo.go"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_this_dispatch) {
    CBMFileResult *r =
        extract_dart_lsp("class Foo { void ping() {} void run() { this.ping(); } }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "Foo.run", "Foo.ping"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_super_dispatch) {
    CBMFileResult *r = extract_dart_lsp("class Base { void ping() {} }\nclass Child extends Base { "
                                        "void run() { super.ping(); } }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "Child.run", "Base.ping"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_mixin_dispatch) {
    CBMFileResult *r =
        extract_dart_lsp("mixin Logging { void log() {} }\nclass Service with Logging {}\n"
                         "void run(Service service) { service.log(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Logging.log"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_interface_dispatch) {
    CBMFileResult *r = extract_dart_lsp("abstract class Runner { void execute(); }\nclass Impl "
                                        "implements Runner { void execute() {} }\n"
                                        "void run(Runner value) { value.execute(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Runner.execute"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_abstract_base_dispatch) {
    CBMFileResult *r = extract_dart_lsp(
        "abstract class Base { String load(); }\nString run(Base value) => value.load();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Base.load"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_extension_dispatch) {
    CBMFileResult *r =
        extract_dart_lsp("class User {}\nextension UserOps on User { void login() {} }\n"
                         "void run(User user) { user.login(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "UserOps.login"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_enum_method_dispatch) {
    CBMFileResult *r = extract_dart_lsp(
        "enum Color { red; String label() => name; }\nString run(Color color) => color.label();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Color.label"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_getter_to_method_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "class Child { void go() {} }\nclass Parent { Child get child => Child(); }\n"
        "void run(Parent parent) { parent.child.go(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Parent.child"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Child.go"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_static_method_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "class Child { void go() {} }\nclass Parent { static Child child() => Child(); }\n"
        "void run() { Parent.child().go(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Parent.child"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Child.go"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_nullable_access) {
    CBMFileResult *r = extract_dart_lsp(
        "class User { void login() {} }\nvoid run(User? user) { user?.login(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "User.login"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_not_null_selector) {
    CBMFileResult *r = extract_dart_lsp(
        "class User { void login() {} }\nvoid run(User? user) { user!.login(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "User.login"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_null_aware_chain) {
    CBMFileResult *r = extract_dart_lsp("class B { void go() {} }\nclass A { B next() => B(); }\n"
                                        "void run(A? value) { value?.next().go(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "A.next"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "B.go"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_cascade_preserves_receiver) {
    CBMFileResult *r = extract_dart_lsp("class Builder { void add() {} void finish() {} }\n"
                                        "void run() { final b = Builder()..add(); b.finish(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Builder.add"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Builder.finish"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_await_then_chain) {
    CBMFileResult *r =
        extract_dart_lsp("class Value { String text() => ''; }\nclass Api { Future<Value> load() "
                         "async => Value(); }\n"
                         "Future<String> run(Api api) async => (await api.load()).text();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Api.load"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Value.text"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_nested_is_promotion) {
    CBMFileResult *r =
        extract_dart_lsp("class Dog { void bark() {} }\nvoid run(Object value, bool ok) { "
                         "if (value is Dog) { if (ok) { value.bark(); } } }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Dog.bark"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_no_promotion_outside_if) {
    CBMFileResult *r = extract_dart_lsp("class Dog { void bark() {} }\nvoid run(Object value) { if "
                                        "(value is Dog) {} value.bark(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "Dog.bark"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_core_show_print) {
    CBMFileResult *r =
        extract_dart_lsp("import 'dart:core' show print;\nvoid run() { print('x'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.core.print"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_core_hide_print) {
    CBMFileResult *r =
        extract_dart_lsp("import 'dart:core' hide print;\nvoid run() { print('x'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "dart.core.print"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_core_prefix_show) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'dart:core' as core show print;\nvoid run() { core.print('x'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.core.print"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_core_prefix_hide) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'dart:core' as core hide print;\nvoid run() { core.print('x'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "dart.core.print"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_core_identical) {
    CBMFileResult *r = extract_dart_lsp("bool run(Object a, Object b) => identical(a, b);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.core.identical"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_prefixed_identical) {
    CBMFileResult *r = extract_dart_lsp("import 'dart:core' as core show identical;\n"
                                        "bool run(Object a, Object b) => core.identical(a, b);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.core.identical"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_string_length_getter) {
    CBMFileResult *r = extract_dart_lsp("int run(String value) => value.length;\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.length"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_map_literal_dispatch) {
    CBMFileResult *r = extract_dart_lsp(
        "bool run() { final value = <String, int>{'a': 1}; return value.containsKey('a'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Map.containsKey"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_set_literal_dispatch) {
    CBMFileResult *r = extract_dart_lsp(
        "bool run() { final value = <String>{'a'}; return value.contains('a'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Set.contains"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_iterable_chain) {
    CBMFileResult *r =
        extract_dart_lsp("List run(List input) => input.where((x) => true).toList();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "List.where"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Iterable.toList"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_int_inherits_object_method) {
    CBMFileResult *r = extract_dart_lsp("String run(int value) => value.toString();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Object.toString"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_bool_inherits_object_method) {
    CBMFileResult *r = extract_dart_lsp("String run(bool value) => value.toString();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Object.toString"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_datetime_now) {
    CBMFileResult *r = extract_dart_lsp("DateTime run() => DateTime.now();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "DateTime.now"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_regexp_constructor_chain) {
    CBMFileResult *r = extract_dart_lsp("bool run(String value) => RegExp('x').hasMatch(value);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "RegExp"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "RegExp.hasMatch"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_top_level_getter_read) {
    CBMFileResult *r =
        extract_dart_lsp("String get title => 'x';\nString run() => title.trim();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "title"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.trim"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_named_constructor_method_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "class Foo { Foo.named(); void go() {} }\nvoid run() { Foo.named().go(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo.named"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo.go"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_three_hop_selector_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "class C { String finish() => ''; }\nclass B { C next() => C(); }\n"
        "class A { B start() => B(); }\nString run(A value) => value.start().next().finish();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "A.start"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "B.next"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "C.finish"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_nested_call_argument) {
    CBMFileResult *r = extract_dart_lsp(
        "String inner() => '';\nvoid outer(String value) {}\nvoid run() { outer(inner()); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "outer"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "inner"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_constructor_argument_call) {
    CBMFileResult *r = extract_dart_lsp(
        "String value() => '';\nclass Foo { Foo(String value); }\nFoo run() => Foo(value());\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "value"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_getter_without_following_call) {
    CBMFileResult *r = extract_dart_lsp(
        "class Foo { String get text => ''; }\nString run(Foo foo) => foo.text;\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo.text"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_unknown_top_level_abstains) {
    CBMFileResult *r = extract_dart_lsp("void run() { totallyUnknown(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "totallyUnknown"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_unknown_known_receiver_abstains) {
    CBMFileResult *r =
        extract_dart_lsp("class Foo { void present() {} }\nvoid run(Foo foo) { foo.absent(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "absent"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_dynamic_initializer_abstains) {
    CBMFileResult *r =
        extract_dart_lsp("void run(dynamic source) { var value = source; value.absent(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "absent"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_hidden_prefixed_type_abstains) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'dart:core' as core hide RegExp;\nvoid run() { core.RegExp('x'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "RegExp"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_empty_file) {
    CBMFileResult *r = extract_dart_lsp("");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->resolved_calls.count, 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_malformed_source_no_crash) {
    CBMFileResult *r = extract_dart_lsp("class Foo { void run( { print('x');\n");
    ASSERT_NOT_NULL(r);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_duration_constructor) {
    CBMFileResult *r = extract_dart_lsp("Duration run() => Duration(seconds: 1);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Duration"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_list_length_getter) {
    CBMFileResult *r = extract_dart_lsp("int run(List value) => value.length;\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "List.length"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stream_listen) {
    CBMFileResult *r = extract_dart_lsp("void run(Stream value) { value.listen((x) {}); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Stream.listen"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_bare_field_type_in_method) {
    CBMFileResult *r = extract_dart_lsp(
        "class User { final String name; User(this.name); String clean() => name.trim(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "clean", "String.trim"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_inherited_field_type) {
    CBMFileResult *r = extract_dart_lsp("class Base { final String name = ''; }\nclass Child "
                                        "extends Base { String clean() => name.trim(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "clean", "String.trim"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_enum_constant_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "enum Color { red; String label() => name; }\nString run() => Color.red.label();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Color.label"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_unknown_property_chain_abstains) {
    CBMFileResult *r = extract_dart_lsp(
        "class Foo { void real() {} }\nvoid run(Foo foo) { foo.missing.real(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "Foo.real"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_instance_method_through_class_abstains) {
    CBMFileResult *r = extract_dart_lsp("class Foo { void go() {} }\nvoid run() { Foo.go(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "Foo.go"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_static_method_through_instance_abstains) {
    CBMFileResult *r = extract_dart_lsp(
        "class Foo { static Foo create() => Foo(); }\nvoid run(Foo foo) { foo.create(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "Foo.create"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_instance_field_through_class_abstains) {
    CBMFileResult *r = extract_dart_lsp(
        "class Foo { final String label = ''; }\nvoid run() { Foo.label.trim(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "String.trim"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_static_field_through_instance_abstains) {
    CBMFileResult *r = extract_dart_lsp(
        "class Foo { static final String label = ''; }\nvoid run(Foo foo) { foo.label.trim(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "String.trim"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_named_only_constructor_has_no_default) {
    CBMFileResult *r = extract_dart_lsp("class Foo { Foo.named(); }\nvoid run() { Foo(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "Foo"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_abstract_generative_constructor_abstains) {
    CBMFileResult *r = extract_dart_lsp("abstract class Foo { Foo(); }\nvoid run() { Foo(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "Foo"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_abstract_factory_constructor_resolves) {
    CBMFileResult *r = extract_dart_lsp("abstract class Foo { factory Foo() = Bar; }\n"
                                        "class Bar implements Foo {}\nFoo run() => Foo();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Foo"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_shadowed_top_level_function_abstains) {
    CBMFileResult *r =
        extract_dart_lsp("void helper() {}\nvoid run() { dynamic helper = null; helper(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "helper"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_unprefixed_hidden_core_type_abstains) {
    CBMFileResult *r =
        extract_dart_lsp("import 'dart:core' hide RegExp;\nvoid run() { RegExp('x'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "RegExp"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_prefixed_type_constructor) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'dart:core' as core show RegExp;\nvoid run() { core.RegExp('x'); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.core.RegExp"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_prefixed_type_static_chain) {
    CBMFileResult *r =
        extract_dart_lsp("import 'dart:core' as core show DateTime;\nString run() => "
                         "core.DateTime.now().toString();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "DateTime.now"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Object.toString"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_relative_import_prefix) {
    const char *source = "import '../shared/service.dart' as svc;\nvoid run() { svc.call(); }\n";
    CBMFileResult *r = extract_dart_lsp_with_external(
        source, "lib/feature/main.dart", "lib/feature/main.dart", "test.lib.shared.service.call");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "test.lib.shared.service.call"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_relative_import_unprefixed_show) {
    const char *source = "import '../shared/service.dart' show call;\nvoid run() { call(); }\n";
    CBMFileResult *r = extract_dart_lsp_with_external(
        source, "lib/feature/main.dart", "lib/feature/main.dart", "test.lib.shared.service.call");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "test.lib.shared.service.call"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_relative_import_derived_from_module) {
    const char *source = "import './service.dart' as svc;\nvoid run() { svc.call(); }\n";
    CBMFileResult *r = extract_dart_lsp_with_external(source, "lib/feature/main.dart", NULL,
                                                      "test.lib.feature.service.call");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "test.lib.feature.service.call"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_package_import_includes_lib_root) {
    const char *source =
        "import 'package:other/src/service.dart' as svc;\nvoid run() { svc.call(); }\n";
    CBMFileResult *r = extract_dart_lsp_with_external(
        source, "lib/feature/main.dart", "lib/feature/main.dart", "other.lib.src.service.call");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "other.lib.src.service.call"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stdlib_string_split_chain) {
    CBMFileResult *r = extract_dart_lsp("String run() => 'a,b'.split(',').first.trim();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.split"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "List.first"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.trim"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stdlib_list_map_to_list) {
    CBMFileResult *r =
        extract_dart_lsp("List run() => [1, 2].map((value) => value).toList();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "List.map"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Iterable.toList"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stdlib_iterable_generic_property_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "String run(Iterable<String> values) => values.toList().first.toUpperCase();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Iterable.toList"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "List.first"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.toUpperCase"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stdlib_json_functions) {
    CBMFileResult *r = extract_dart_lsp("import 'dart:convert';\n"
                                        "String run(Object value) { jsonDecode('{}'); return "
                                        "jsonEncode(value); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.convert.jsonDecode"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.convert.jsonEncode"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stdlib_prefixed_json_functions) {
    CBMFileResult *r = extract_dart_lsp("import 'dart:convert' as convert;\n"
                                        "String run(Object value) => convert.jsonEncode(value);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.convert.jsonEncode"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stdlib_future_then_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "Future<String> run(Future<int> value) => value.then((number) => '$number')"
        ".whenComplete(() {});\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Future.then"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Future.whenComplete"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stdlib_io_file_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'dart:io';\nFuture<String> run() => File('a.txt').readAsString();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.io.File"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "File.readAsString"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stdlib_math_random) {
    CBMFileResult *r =
        extract_dart_lsp("import 'dart:math';\nint run() => Random().nextInt(10);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dart.math.Random"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Random.nextInt"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_stdlib_typed_data) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'dart:typed_data';\nByteBuffer run() => Uint8List(4).buffer;\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Uint8List"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Uint8List.buffer"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_flutter_navigator_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:flutter/material.dart';\n"
        "Future<void> run(BuildContext context, Route<void> route) => "
        "Navigator.of(context).push(route);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Navigator.of"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "NavigatorState.push"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_flutter_bare_set_state) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:flutter/material.dart';\n"
        "class Screen extends StatefulWidget {}\n"
        "class ScreenState extends State<Screen> { void refresh() { setState(() {}); } }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "refresh", "State.setState"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_flutter_widget_constructors) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:flutter/material.dart';\n"
        "Widget run() => Container(child: Row(children: [Text('hello')]));\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "material.Container"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "material.Row"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "material.Text"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_flutter_named_constructors_and_color) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:flutter/material.dart';\n"
        "Widget run() { final color = Colors.red; final padding = EdgeInsets.all(8); "
        "return ListView.builder(itemBuilder: (_, i) => Text('$i')); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Colors.red"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "EdgeInsets.all"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "ListView.builder"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_flutter_dialog_functions) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:flutter/material.dart';\n"
        "void run(BuildContext context) { showDialog(context: context, builder: (_) => "
        "Text('x')); showModalBottomSheet(context: context, builder: (_) => Text('y')); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "material.showDialog"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "material.showModalBottomSheet"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_flutter_theme_media_query_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:flutter/material.dart';\n"
        "double run(BuildContext context) { Theme.of(context).copyWith(); return "
        "MediaQuery.of(context).size.width; }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "Theme.of"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "ThemeData.copyWith"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "MediaQuery.of"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Size.width"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_http_client_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:http/http.dart' as http;\n"
        "Future<http.Response> run(Uri uri) => http.Client().get(uri);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "http.lib.http.Client"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "BaseClient.get"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_http_await_response_property) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:http/http.dart' as http;\n"
        "Future<String> run(Uri uri) async { final response = await http.get(uri); "
        "return response.body.trim(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "http.lib.http.get"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Response.body"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "String.trim"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_dio_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:dio/dio.dart';\nFuture<Response> run() => Dio().get('/users');\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "dio.lib.dio.Dio"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Dio.get"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_provider_and_change_notifier) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:flutter/material.dart';\nimport 'package:provider/provider.dart';\n"
        "class Model extends ChangeNotifier { void update() { notifyListeners(); } }\n"
        "Model run(BuildContext context) => Provider.of<Model>(context);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "update", "ChangeNotifier.notifyListeners"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "Provider.of"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_riverpod_ref_calls) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:flutter_riverpod/flutter_riverpod.dart';\n"
        "void run(WidgetRef ref, Object provider) { ref.watch(provider); ref.read(provider); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "WidgetRef.watch"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "WidgetRef.read"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_bloc_calls) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:flutter/material.dart';\nimport 'package:flutter_bloc/flutter_bloc.dart';\n"
        "class Counter extends Cubit<int> { Counter() : super(0); void increment() { emit(1); } }\n"
        "Counter run(BuildContext context) => BlocProvider.of<Counter>(context);\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "increment", "BlocBase.emit"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "BlocProvider.of"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_get_it_static_getter_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:get_it/get_it.dart';\nObject run() => GetIt.instance.get<Object>();\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "GetIt.instance"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "GetIt.get"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_shared_preferences_await_chain) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:shared_preferences/shared_preferences.dart';\n"
        "Future<String> run() async { final prefs = await SharedPreferences.getInstance(); "
        "prefs.setString('key', 'value'); return prefs.getString('key') ?? ''; }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_GTE(dart_require_resolved(r, "run", "SharedPreferences.getInstance"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "SharedPreferences.setString"), 0);
    ASSERT_GTE(dart_require_resolved(r, "run", "SharedPreferences.getString"), 0);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_unimported_flutter_abstains) {
    CBMFileResult *r = extract_dart_lsp("void run(dynamic context) { Navigator.of(context); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "Navigator.of"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_hidden_package_symbol_abstains) {
    CBMFileResult *r = extract_dart_lsp(
        "import 'package:http/http.dart' hide Client;\nvoid run() { Client(); }\n");
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(dart_find_resolved(r, "run", "http.lib.http.Client"), -1);
    cbm_free_result(r);
    PASS();
}

TEST(dartlsp_cross_package_bare_call) {
    const char *source =
        "import 'package:fixture/src/service.dart' show fetchRemote;\n"
        "void run() { fetchRemote(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.main.run",
         .short_name = "run",
         .label = "Function",
         .def_module_qn = "test.lib.main",
         .return_types = "void",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.src.service.fetchRemote",
         .short_name = "fetchRemote",
         .label = "Function",
         .def_module_qn = "test.lib.src.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {"package:fixture/src/service.dart"};
    const char *import_qns[] = {"test.lib.src.service"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 2,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.main.run", "test.lib.src.service.fetchRemote"),
               0);
    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.main.run", "fixture.lib.src.service.fetchRemote"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_relative_bare_call) {
    const char *source =
        "import '../shared/service.dart' show fetchRelative;\n"
        "void run() { fetchRelative(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.feature.main.run",
         .short_name = "run",
         .label = "Function",
         .def_module_qn = "test.lib.feature.main",
         .return_types = "void",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.shared.service.fetchRelative",
         .short_name = "fetchRelative",
         .label = "Function",
         .def_module_qn = "test.lib.shared.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {"../shared/service.dart"};
    const char *import_qns[] = {"test.lib.shared.service"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.feature.main", defs, 2,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.feature.main.run", "test.lib.shared.service.fetchRelative"),
               0);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_prefix_show_hide) {
    const char *source =
        "import '../shared/service.dart' as svc show allowed hide hidden;\n"
        "void prefixed() { svc.allowed(); svc.hidden(); }\n"
        "void bare() { allowed(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.shared.service.allowed",
         .short_name = "allowed",
         .label = "Function",
         .def_module_qn = "test.lib.shared.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.shared.service.hidden",
         .short_name = "hidden",
         .label = "Function",
         .def_module_qn = "test.lib.shared.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {"../shared/service.dart"};
    const char *import_qns[] = {"test.lib.shared.service"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.feature.main", defs, 2,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.feature.main.prefixed", "test.lib.shared.service.allowed"),
               0);
    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.feature.main.prefixed", "test.lib.shared.service.hidden"),
              -1);
    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.feature.main.bare", "test.lib.shared.service.allowed"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_reexport_bare_call) {
    const char *source =
        "import 'api.dart' show publicHelper;\n"
        "void run() { publicHelper(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.main.run",
         .short_name = "run",
         .label = "Function",
         .def_module_qn = "test.lib.main",
         .return_types = "void",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.src.service.publicHelper",
         .short_name = "publicHelper",
         .label = "Function",
         .def_module_qn = "test.lib.src.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {"api.dart", "api.dart"};
    const char *import_qns[] = {"test.lib.api", "test.lib.src.service"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 2,
                           import_names, import_qns, 2, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.main.run", "test.lib.src.service.publicHelper"),
               0);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_part_private_method) {
    const char *source =
        "part 'model.g.dart';\n"
        "int run(Generated value) => value._read();\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.model.g.Generated",
         .short_name = "Generated",
         .label = "Class",
         .def_module_qn = "test.lib.model.g",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.model.g.Generated._read",
         .short_name = "_read",
         .label = "Method",
         .receiver_type = "test.lib.model.g.Generated",
         .def_module_qn = "test.lib.model.g",
         .return_types = "int",
         .lang = CBM_LANG_DART,
         .callable_flags = CBM_DEF_CALLABLE_KNOWN},
    };
    const char *import_names[] = {"model.g.dart"};
    const char *import_qns[] = {"test.lib.model.g"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.model", defs, 2,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.model.run", "test.lib.model.g.Generated._read"),
               0);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_part_of_owner_private) {
    const char *source =
        "part of 'model.dart';\n"
        "int generated() => _ownerPrivate();\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.model._ownerPrivate",
         .short_name = "_ownerPrivate",
         .label = "Function",
         .def_module_qn = "test.lib.model",
         .return_types = "int",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {"model.dart"};
    const char *import_qns[] = {"test.lib.model"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.model.g", defs, 1,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.model.g.generated", "test.lib.model._ownerPrivate"),
               0);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_regular_import_private_abstains) {
    const char *source =
        "import 'model.dart';\n"
        "int run() => _ownerPrivate();\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.model._ownerPrivate",
         .short_name = "_ownerPrivate",
         .label = "Function",
         .def_module_qn = "test.lib.model",
         .return_types = "int",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {"model.dart"};
    const char *import_qns[] = {"test.lib.model"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.other", defs, 1,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.other.run", "test.lib.model._ownerPrivate"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_inherited_method_dispatch) {
    const char *source =
        "import 'animals.dart' show Dog;\n"
        "void run(Dog dog) { dog.speak(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.animals.Animal",
         .short_name = "Animal",
         .label = "Class",
         .def_module_qn = "test.lib.animals",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.animals.Dog",
         .short_name = "Dog",
         .label = "Class",
         .def_module_qn = "test.lib.animals",
         .embedded_types = "test.lib.animals.Animal",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.animals.Animal.speak",
         .short_name = "speak",
         .label = "Method",
         .receiver_type = "test.lib.animals.Animal",
         .def_module_qn = "test.lib.animals",
         .return_types = "void",
         .lang = CBM_LANG_DART,
         .callable_flags = CBM_DEF_CALLABLE_KNOWN},
    };
    const char *import_names[] = {"animals.dart"};
    const char *import_qns[] = {"test.lib.animals"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 3,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.main.run", "test.lib.animals.Animal.speak"),
               0);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_return_type_chain) {
    const char *source =
        "import 'client.dart' show makeClient;\n"
        "void run() { makeClient().send(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.client.Client",
         .short_name = "Client",
         .label = "Class",
         .def_module_qn = "test.lib.client",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.client.makeClient",
         .short_name = "makeClient",
         .label = "Function",
         .def_module_qn = "test.lib.client",
         .return_types = "Client",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.client.Client.send",
         .short_name = "send",
         .label = "Method",
         .receiver_type = "test.lib.client.Client",
         .def_module_qn = "test.lib.client",
         .return_types = "void",
         .lang = CBM_LANG_DART,
         .callable_flags = CBM_DEF_CALLABLE_KNOWN},
    };
    const char *import_names[] = {"client.dart"};
    const char *import_qns[] = {"test.lib.client"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 3,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.main.run", "test.lib.client.makeClient"),
               0);
    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.main.run", "test.lib.client.Client.send"),
               0);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_conditional_import_abstains) {
    const char *source =
        "import 'stub.dart' if (dart.library.io) 'io.dart' show platformCall;\n"
        "void run() { platformCall(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.stub.platformCall",
         .short_name = "platformCall",
         .label = "Function",
         .def_module_qn = "test.lib.stub",
         .return_types = "void",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.io.platformCall",
         .short_name = "platformCall",
         .label = "Function",
         .def_module_qn = "test.lib.io",
         .return_types = "void",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {"stub.dart", "io.dart"};
    const char *import_qns[] = {"test.lib.stub", "test.lib.io"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 2,
                           import_names, import_qns, 2, NULL, &out);

    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.main.run", "test.lib.stub.platformCall"),
              -1);
    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.main.run", "test.lib.io.platformCall"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_local_core_name_shadowing) {
    const char *source =
        "import 'domain.dart' show makeString, makeFuture;\n"
        "void run() { makeString().localOnly(); makeFuture().localOnly(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.domain.String",
         .short_name = "String",
         .label = "Class",
         .def_module_qn = "test.lib.domain",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.domain.String.localOnly",
         .short_name = "localOnly",
         .label = "Method",
         .receiver_type = "test.lib.domain.String",
         .def_module_qn = "test.lib.domain",
         .return_types = "void",
         .lang = CBM_LANG_DART,
         .callable_flags = CBM_DEF_CALLABLE_KNOWN},
        {.qualified_name = "test.lib.domain.Future",
         .short_name = "Future",
         .label = "Class",
         .def_module_qn = "test.lib.domain",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.domain.Future.localOnly",
         .short_name = "localOnly",
         .label = "Method",
         .receiver_type = "test.lib.domain.Future",
         .def_module_qn = "test.lib.domain",
         .return_types = "void",
         .lang = CBM_LANG_DART,
         .callable_flags = CBM_DEF_CALLABLE_KNOWN},
        {.qualified_name = "test.lib.domain.makeString",
         .short_name = "makeString",
         .label = "Function",
         .def_module_qn = "test.lib.domain",
         .return_types = "String",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.domain.makeFuture",
         .short_name = "makeFuture",
         .label = "Function",
         .def_module_qn = "test.lib.domain",
         .return_types = "Future",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {"domain.dart"};
    const char *import_qns[] = {"test.lib.domain"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 6,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.main.run", "test.lib.domain.String.localOnly"),
               0);
    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.main.run", "test.lib.domain.Future.localOnly"),
               0);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_static_method_receiver_kind) {
    const char *source =
        "import 'tools.dart' show Tool;\n"
        "void invalid(Tool tool) { tool.reset(); }\n"
        "void valid() { Tool.reset(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.tools.Tool",
         .short_name = "Tool",
         .label = "Class",
         .def_module_qn = "test.lib.tools",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.tools.Tool.reset",
         .short_name = "reset",
         .label = "Method",
         .receiver_type = "test.lib.tools.Tool",
         .def_module_qn = "test.lib.tools",
         .return_types = "void",
         .lang = CBM_LANG_DART,
         .callable_flags = CBM_DEF_CALLABLE_KNOWN | CBM_DEF_CALLABLE_STATIC},
    };
    const char *import_names[] = {"tools.dart"};
    const char *import_qns[] = {"test.lib.tools"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 2,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.main.invalid", "test.lib.tools.Tool.reset"),
              -1);
    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.main.valid", "test.lib.tools.Tool.reset"),
               0);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_private_imported_base_member_abstains) {
    const char *source =
        "import 'external.dart' show ExternalBase;\n"
        "class Child extends ExternalBase {}\n"
        "void run(Child child) { child._secret(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.external.ExternalBase",
         .short_name = "ExternalBase",
         .label = "Class",
         .def_module_qn = "test.lib.external",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.external.ExternalBase._secret",
         .short_name = "_secret",
         .label = "Method",
         .receiver_type = "test.lib.external.ExternalBase",
         .def_module_qn = "test.lib.external",
         .return_types = "void",
         .lang = CBM_LANG_DART,
         .callable_flags = CBM_DEF_CALLABLE_KNOWN},
    };
    const char *import_names[] = {"external.dart"};
    const char *import_qns[] = {"test.lib.external"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 2,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.main.run", "test.lib.external.ExternalBase._secret"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_hide_overflow_abstains) {
    const char *source =
        "import 'service.dart' hide "
        "h00, h01, h02, h03, h04, h05, h06, h07, "
        "h08, h09, h10, h11, h12, h13, h14, h15, "
        "h16, h17, h18, h19, h20, h21, h22, h23, "
        "h24, h25, h26, h27, h28, h29, h30, h31, "
        "h32, h33, h34, h35, h36, h37, h38, h39, "
        "h40, h41, h42, h43, h44, h45, h46, h47, "
        "h48, h49, h50, h51, h52, h53, h54, h55, "
        "h56, h57, h58, h59, h60, h61, h62, h63, blocked;\n"
        "void run() { blocked(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.service.blocked",
         .short_name = "blocked",
         .label = "Function",
         .def_module_qn = "test.lib.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {"service.dart"};
    const char *import_qns[] = {"test.lib.service"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 1,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.main.run", "test.lib.service.blocked"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_part_inherits_implicit_core) {
    const char *source =
        "part of 'model.dart';\n"
        "void generated(String value) { value.toUpperCase(); }\n";
    const char *import_names[] = {"model.dart"};
    const char *import_qns[] = {"test.lib.model"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.model.g", NULL, 0,
                           import_names, import_qns, 1, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.model.g.generated", "dart.core.String.toUpperCase"),
               0);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_part_restricted_core_abstains) {
    const char *source =
        "part of 'model.dart';\n"
        "void generated(String value) { value.toUpperCase(); }\n";
    const char *import_names[] = {"model.dart", "@dart-part-core-blocked"};
    const char *import_qns[] = {"test.lib.model", "dart.core"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.model.g", NULL, 0,
                           import_names, import_qns, 2, NULL, &out);

    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.model.g.generated", "dart.core.String.toUpperCase"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_reexport_show_hide) {
    const char *source =
        "import 'barrel.dart' show allowed, hidden;\n"
        "void run() { allowed(); hidden(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.service.allowed",
         .short_name = "allowed",
         .label = "Function",
         .def_module_qn = "test.lib.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.service.hidden",
         .short_name = "hidden",
         .label = "Function",
         .def_module_qn = "test.lib.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {
        "barrel.dart", "@dart-export-filter:S:allowed;H:hidden;|barrel.dart"};
    const char *import_qns[] = {"test.lib.barrel", "test.lib.service"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.main", defs, 2,
                           import_names, import_qns, 2, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.main.run", "test.lib.service.allowed"),
               0);
    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.main.run", "test.lib.service.hidden"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_part_external_package_collision_abstains) {
    const char *source =
        "part of 'owner.dart';\n"
        "void generated() { externalHelper(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "dep.lib.api.externalHelper",
         .short_name = "externalHelper",
         .label = "Function",
         .def_module_qn = "dep.lib.api",
         .return_types = "void",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {
        "owner.dart", "@dart-part-import:package:dep/api.dart"};
    const char *import_qns[] = {
        "dep.lib.owner", "@dart-external-module:dep.lib.api"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "dep.lib.owner.g", defs, 1,
                           import_names, import_qns, 2, NULL, &out);

    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "dep.lib.owner.g.generated", "dep.lib.api.externalHelper"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

TEST(dartlsp_cross_part_inherits_prefixed_filter) {
    const char *source =
        "part of 'owner.dart';\n"
        "void generated() { svc.allowed(); svc.hidden(); }\n";
    CBMLSPDef defs[] = {
        {.qualified_name = "test.lib.service.allowed",
         .short_name = "allowed",
         .label = "Function",
         .def_module_qn = "test.lib.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
        {.qualified_name = "test.lib.service.hidden",
         .short_name = "hidden",
         .label = "Function",
         .def_module_qn = "test.lib.service",
         .return_types = "void",
         .lang = CBM_LANG_DART},
    };
    const char *import_names[] = {
        "owner.dart", "@dart-part-filter:P:svc;S:allowed;H:hidden;|service.dart"};
    const char *import_qns[] = {"test.lib.owner", "test.lib.service"};
    CBMArena arena;
    cbm_arena_init(&arena);
    CBMResolvedCallArray out = {0};

    cbm_run_dart_lsp_cross(&arena, source, (int)strlen(source), "test.lib.owner.g", defs, 2,
                           import_names, import_qns, 2, NULL, &out);

    ASSERT_GTE(dart_require_resolved_arr_exact(
                   &out, "test.lib.owner.g.generated", "test.lib.service.allowed"),
               0);
    ASSERT_EQ(dart_find_resolved_arr_exact(
                  &out, "test.lib.owner.g.generated", "test.lib.service.hidden"),
              -1);
    cbm_arena_destroy(&arena);
    PASS();
}

SUITE(dart_lsp) {
    RUN_TEST(dartlsp_core_print);
    RUN_TEST(dartlsp_local_top_level_call);
    RUN_TEST(dartlsp_default_constructor);
    RUN_TEST(dartlsp_named_constructor);
    RUN_TEST(dartlsp_typed_receiver_method);
    RUN_TEST(dartlsp_inferred_constructor_type);
    RUN_TEST(dartlsp_selector_chain_fold);
    RUN_TEST(dartlsp_getter_property);
    RUN_TEST(dartlsp_inherited_method);
    RUN_TEST(dartlsp_cascade_methods);
    RUN_TEST(dartlsp_await_future_unwrap);
    RUN_TEST(dartlsp_is_promotion);
    RUN_TEST(dartlsp_string_literal_dispatch);
    RUN_TEST(dartlsp_list_literal_dispatch);
    RUN_TEST(dartlsp_unknown_receiver_abstains);
    RUN_TEST(dartlsp_factory_constructor);
    RUN_TEST(dartlsp_redirecting_factory_constructor);
    RUN_TEST(dartlsp_private_named_constructor);
    RUN_TEST(dartlsp_static_factory_method);
    RUN_TEST(dartlsp_const_constructor);
    RUN_TEST(dartlsp_final_typed_local);
    RUN_TEST(dartlsp_var_factory_inference);
    RUN_TEST(dartlsp_const_string_inference);
    RUN_TEST(dartlsp_declared_field_dispatch);
    RUN_TEST(dartlsp_static_field_chain);
    RUN_TEST(dartlsp_this_dispatch);
    RUN_TEST(dartlsp_super_dispatch);
    RUN_TEST(dartlsp_mixin_dispatch);
    RUN_TEST(dartlsp_interface_dispatch);
    RUN_TEST(dartlsp_abstract_base_dispatch);
    RUN_TEST(dartlsp_extension_dispatch);
    RUN_TEST(dartlsp_enum_method_dispatch);
    RUN_TEST(dartlsp_getter_to_method_chain);
    RUN_TEST(dartlsp_static_method_chain);
    RUN_TEST(dartlsp_nullable_access);
    RUN_TEST(dartlsp_not_null_selector);
    RUN_TEST(dartlsp_null_aware_chain);
    RUN_TEST(dartlsp_cascade_preserves_receiver);
    RUN_TEST(dartlsp_await_then_chain);
    RUN_TEST(dartlsp_nested_is_promotion);
    RUN_TEST(dartlsp_no_promotion_outside_if);
    RUN_TEST(dartlsp_core_show_print);
    RUN_TEST(dartlsp_core_hide_print);
    RUN_TEST(dartlsp_core_prefix_show);
    RUN_TEST(dartlsp_core_prefix_hide);
    RUN_TEST(dartlsp_core_identical);
    RUN_TEST(dartlsp_prefixed_identical);
    RUN_TEST(dartlsp_string_length_getter);
    RUN_TEST(dartlsp_map_literal_dispatch);
    RUN_TEST(dartlsp_set_literal_dispatch);
    RUN_TEST(dartlsp_iterable_chain);
    RUN_TEST(dartlsp_int_inherits_object_method);
    RUN_TEST(dartlsp_bool_inherits_object_method);
    RUN_TEST(dartlsp_datetime_now);
    RUN_TEST(dartlsp_regexp_constructor_chain);
    RUN_TEST(dartlsp_top_level_getter_read);
    RUN_TEST(dartlsp_named_constructor_method_chain);
    RUN_TEST(dartlsp_three_hop_selector_chain);
    RUN_TEST(dartlsp_nested_call_argument);
    RUN_TEST(dartlsp_constructor_argument_call);
    RUN_TEST(dartlsp_getter_without_following_call);
    RUN_TEST(dartlsp_unknown_top_level_abstains);
    RUN_TEST(dartlsp_unknown_known_receiver_abstains);
    RUN_TEST(dartlsp_dynamic_initializer_abstains);
    RUN_TEST(dartlsp_hidden_prefixed_type_abstains);
    RUN_TEST(dartlsp_empty_file);
    RUN_TEST(dartlsp_malformed_source_no_crash);
    RUN_TEST(dartlsp_duration_constructor);
    RUN_TEST(dartlsp_list_length_getter);
    RUN_TEST(dartlsp_stream_listen);
    RUN_TEST(dartlsp_bare_field_type_in_method);
    RUN_TEST(dartlsp_inherited_field_type);
    RUN_TEST(dartlsp_enum_constant_chain);
    RUN_TEST(dartlsp_unknown_property_chain_abstains);
    RUN_TEST(dartlsp_instance_method_through_class_abstains);
    RUN_TEST(dartlsp_static_method_through_instance_abstains);
    RUN_TEST(dartlsp_instance_field_through_class_abstains);
    RUN_TEST(dartlsp_static_field_through_instance_abstains);
    RUN_TEST(dartlsp_named_only_constructor_has_no_default);
    RUN_TEST(dartlsp_abstract_generative_constructor_abstains);
    RUN_TEST(dartlsp_abstract_factory_constructor_resolves);
    RUN_TEST(dartlsp_shadowed_top_level_function_abstains);
    RUN_TEST(dartlsp_unprefixed_hidden_core_type_abstains);
    RUN_TEST(dartlsp_prefixed_type_constructor);
    RUN_TEST(dartlsp_prefixed_type_static_chain);
    RUN_TEST(dartlsp_relative_import_prefix);
    RUN_TEST(dartlsp_relative_import_unprefixed_show);
    RUN_TEST(dartlsp_relative_import_derived_from_module);
    RUN_TEST(dartlsp_package_import_includes_lib_root);
    RUN_TEST(dartlsp_stdlib_string_split_chain);
    RUN_TEST(dartlsp_stdlib_list_map_to_list);
    RUN_TEST(dartlsp_stdlib_iterable_generic_property_chain);
    RUN_TEST(dartlsp_stdlib_json_functions);
    RUN_TEST(dartlsp_stdlib_prefixed_json_functions);
    RUN_TEST(dartlsp_stdlib_future_then_chain);
    RUN_TEST(dartlsp_stdlib_io_file_chain);
    RUN_TEST(dartlsp_stdlib_math_random);
    RUN_TEST(dartlsp_stdlib_typed_data);
    RUN_TEST(dartlsp_flutter_navigator_chain);
    RUN_TEST(dartlsp_flutter_bare_set_state);
    RUN_TEST(dartlsp_flutter_widget_constructors);
    RUN_TEST(dartlsp_flutter_named_constructors_and_color);
    RUN_TEST(dartlsp_flutter_dialog_functions);
    RUN_TEST(dartlsp_flutter_theme_media_query_chain);
    RUN_TEST(dartlsp_http_client_chain);
    RUN_TEST(dartlsp_http_await_response_property);
    RUN_TEST(dartlsp_dio_chain);
    RUN_TEST(dartlsp_provider_and_change_notifier);
    RUN_TEST(dartlsp_riverpod_ref_calls);
    RUN_TEST(dartlsp_bloc_calls);
    RUN_TEST(dartlsp_get_it_static_getter_chain);
    RUN_TEST(dartlsp_shared_preferences_await_chain);
    RUN_TEST(dartlsp_unimported_flutter_abstains);
    RUN_TEST(dartlsp_hidden_package_symbol_abstains);
    RUN_TEST(dartlsp_cross_package_bare_call);
    RUN_TEST(dartlsp_cross_relative_bare_call);
    RUN_TEST(dartlsp_cross_prefix_show_hide);
    RUN_TEST(dartlsp_cross_reexport_bare_call);
    RUN_TEST(dartlsp_cross_part_private_method);
    RUN_TEST(dartlsp_cross_part_of_owner_private);
    RUN_TEST(dartlsp_cross_regular_import_private_abstains);
    RUN_TEST(dartlsp_cross_inherited_method_dispatch);
    RUN_TEST(dartlsp_cross_return_type_chain);
    RUN_TEST(dartlsp_cross_conditional_import_abstains);
    RUN_TEST(dartlsp_cross_local_core_name_shadowing);
    RUN_TEST(dartlsp_cross_static_method_receiver_kind);
    RUN_TEST(dartlsp_cross_private_imported_base_member_abstains);
    RUN_TEST(dartlsp_cross_hide_overflow_abstains);
    RUN_TEST(dartlsp_cross_part_inherits_implicit_core);
    RUN_TEST(dartlsp_cross_part_restricted_core_abstains);
    RUN_TEST(dartlsp_cross_reexport_show_hide);
    RUN_TEST(dartlsp_cross_part_external_package_collision_abstains);
    RUN_TEST(dartlsp_cross_part_inherits_prefixed_filter);
}
