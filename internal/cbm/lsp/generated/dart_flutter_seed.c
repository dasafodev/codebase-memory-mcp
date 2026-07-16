/*
 * dart_flutter_seed.c - Conservative Flutter and common-package API seeds.
 *
 * These public import-surface shapes let the per-file Dart pass resolve the
 * most common framework and package chains without reading a pub cache. Only
 * stable APIs with unambiguous ownership are registered.
 *
 * This file is included from lsp_all.c.
 */

#include "../dart_lsp.h"

#include <string.h>

static const CBMType *dart_flutter_signature(CBMArena *arena, const CBMType *return_type) {
    const CBMType *returns[] = {return_type ? return_type : cbm_type_unknown(), NULL};
    return cbm_type_func(arena, NULL, NULL, returns);
}

static const CBMType *dart_flutter_t1(CBMArena *arena, const char *qn, const CBMType *arg) {
    const CBMType *args[] = {arg};
    return cbm_type_template(arena, qn, args, 1);
}

static void dart_flutter_add_type(CBMTypeRegistry *registry, CBMArena *arena, const char *qn,
                                  const char *parent, const char *type_param, bool is_interface) {
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
    if (type_param) {
        const char **params = cbm_arena_alloc(arena, 2 * sizeof(*params));
        params[0] = type_param;
        params[1] = NULL;
        type.type_param_names = params;
    }
    cbm_registry_add_type(registry, type);
}

static void dart_flutter_add_method(CBMTypeRegistry *registry, CBMArena *arena,
                                    const char *receiver, const char *name,
                                    const CBMType *return_type, int flags,
                                    const char *type_param) {
    CBMRegisteredFunc func = {0};
    func.receiver_type = receiver;
    func.short_name = name;
    func.qualified_name = cbm_arena_sprintf(arena, "%s.%s", receiver, name);
    func.signature = dart_flutter_signature(arena, return_type);
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

static void dart_flutter_add_function(CBMTypeRegistry *registry, CBMArena *arena,
                                      const char *library, const char *name,
                                      const CBMType *return_type, const char *type_param) {
    CBMRegisteredFunc func = {0};
    func.short_name = name;
    func.qualified_name = cbm_arena_sprintf(arena, "%s.%s", library, name);
    func.signature = dart_flutter_signature(arena, return_type);
    func.min_params = 0;
    if (type_param) {
        const char **params = cbm_arena_alloc(arena, 2 * sizeof(*params));
        params[0] = type_param;
        params[1] = NULL;
        func.type_param_names = params;
    }
    cbm_registry_add_func(registry, func);
}

static void dart_flutter_add_constructor(CBMTypeRegistry *registry, CBMArena *arena,
                                         const char *type_qn, const char *name,
                                         const CBMType *return_type, bool factory) {
    CBMRegisteredFunc func = {0};
    func.receiver_type = type_qn;
    func.short_name = name ? name : "<init>";
    func.qualified_name = name ? cbm_arena_sprintf(arena, "%s.%s", type_qn, name) : type_qn;
    func.signature = dart_flutter_signature(arena, return_type);
    func.min_params = 0;
    func.flags = CBM_FUNC_FLAG_STATICMETHOD | CBM_DART_FUNC_FLAG_CONSTRUCTOR;
    if (factory) {
        func.flags |= CBM_DART_FUNC_FLAG_FACTORY;
    }
    cbm_registry_add_func(registry, func);
}

static void dart_flutter_register_framework(CBMTypeRegistry *registry, CBMArena *arena) {
    const char *lib = "flutter.lib.material";
    const CBMType *void_t = cbm_type_named(arena, "dart.core.void");
    const CBMType *bool_t = cbm_type_named(arena, "dart.core.bool");
    const CBMType *int_t = cbm_type_named(arena, "dart.core.int");
    const CBMType *double_t = cbm_type_named(arena, "dart.core.double");
    const CBMType *string_t = cbm_type_named(arena, "dart.core.String");
    const CBMType *t_t = cbm_type_type_param(arena, "T");
    const CBMType *widget_t = cbm_type_named(arena, "flutter.lib.material.Widget");
    const CBMType *context_t = cbm_type_named(arena, "flutter.lib.material.BuildContext");
    const CBMType *navigator_state_t =
        cbm_type_named(arena, "flutter.lib.material.NavigatorState");
    const CBMType *theme_data_t = cbm_type_named(arena, "flutter.lib.material.ThemeData");
    const CBMType *media_data_t = cbm_type_named(arena, "flutter.lib.material.MediaQueryData");
    const CBMType *color_t = cbm_type_named(arena, "flutter.lib.material.Color");
    const CBMType *future_t = dart_flutter_t1(arena, "dart.async.Future", t_t);

    static const struct {
        const char *name;
        const char *parent;
        const char *param;
        bool interface_type;
    } types[] = {{"Key", "dart.core.Object", NULL, true},
                 {"ValueKey", "flutter.lib.material.Key", "T", false},
                 {"GlobalKey", "flutter.lib.material.Key", "T", false},
                 {"BuildContext", "dart.core.Object", NULL, true},
                 {"Widget", "dart.core.Object", NULL, true},
                 {"StatelessWidget", "flutter.lib.material.Widget", NULL, true},
                 {"StatefulWidget", "flutter.lib.material.Widget", NULL, true},
                 {"State", "dart.core.Object", "T", true},
                 {"Navigator", "flutter.lib.material.StatefulWidget", NULL, false},
                 {"NavigatorState", "flutter.lib.material.State", NULL, false},
                 {"Route", "dart.core.Object", "T", true},
                 {"MaterialPageRoute", "flutter.lib.material.Route", "T", false},
                 {"Scaffold", "flutter.lib.material.StatefulWidget", NULL, false},
                 {"ScaffoldState", "flutter.lib.material.State", NULL, false},
                 {"AppBar", "flutter.lib.material.StatefulWidget", NULL, false},
                 {"Text", "flutter.lib.material.StatelessWidget", NULL, false},
                 {"Container", "flutter.lib.material.StatelessWidget", NULL, false},
                 {"Row", "flutter.lib.material.StatelessWidget", NULL, false},
                 {"Column", "flutter.lib.material.StatelessWidget", NULL, false},
                 {"ListView", "flutter.lib.material.StatelessWidget", NULL, false},
                 {"GestureDetector", "flutter.lib.material.StatelessWidget", NULL, false},
                 {"TextEditingController", "flutter.lib.material.ChangeNotifier", NULL, false},
                 {"ScrollController", "flutter.lib.material.ChangeNotifier", NULL, false},
                 {"AnimationController", "flutter.lib.material.Animation", NULL, false},
                 {"Animation", "flutter.lib.material.Listenable", "T", true},
                 {"Listenable", "dart.core.Object", NULL, true},
                 {"ChangeNotifier", "flutter.lib.material.Listenable", NULL, false},
                 {"Theme", "flutter.lib.material.InheritedWidget", NULL, false},
                 {"ThemeData", "dart.core.Object", NULL, false},
                 {"MediaQuery", "flutter.lib.material.InheritedWidget", NULL, false},
                 {"MediaQueryData", "dart.core.Object", NULL, false},
                 {"InheritedWidget", "flutter.lib.material.Widget", NULL, true},
                 {"Color", "dart.core.Object", NULL, false},
                 {"Colors", "dart.core.Object", NULL, false},
                 {"EdgeInsets", "dart.core.Object", NULL, false},
                 {"MaterialApp", "flutter.lib.material.StatefulWidget", NULL, false},
                 {"TextStyle", "dart.core.Object", NULL, false},
                 {"Size", "dart.core.Object", NULL, false},
                 {"TickerFuture", "dart.core.Object", NULL, false},
                 {NULL, NULL, NULL, false}};
    for (int i = 0; types[i].name; i++) {
        const char *qn = cbm_arena_sprintf(arena, "%s.%s", lib, types[i].name);
        dart_flutter_add_type(registry, arena, qn, types[i].parent, types[i].param,
                              types[i].interface_type);
    }

    dart_flutter_add_method(registry, arena, "flutter.lib.material.StatelessWidget", "build",
                            widget_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.StatefulWidget", "createState",
                            cbm_type_named(arena, "flutter.lib.material.State"), 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.State", "setState", void_t, 0,
                            NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.State", "initState", void_t, 0,
                            NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.State", "dispose", void_t, 0,
                            NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.State", "build", widget_t, 0,
                            NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.State", "context", context_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.State", "widget", t_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.State", "mounted", bool_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);

    dart_flutter_add_method(registry, arena, "flutter.lib.material.Navigator", "of",
                            navigator_state_t, CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.Navigator", "push", future_t,
                            CBM_FUNC_FLAG_STATICMETHOD, "T");
    dart_flutter_add_method(registry, arena, "flutter.lib.material.Navigator", "pushNamed",
                            future_t, CBM_FUNC_FLAG_STATICMETHOD, "T");
    dart_flutter_add_method(registry, arena, "flutter.lib.material.Navigator", "pop", void_t,
                            CBM_FUNC_FLAG_STATICMETHOD, "T");
    dart_flutter_add_method(registry, arena, "flutter.lib.material.NavigatorState", "push",
                            future_t, 0, "T");
    dart_flutter_add_method(registry, arena, "flutter.lib.material.NavigatorState", "pushNamed",
                            future_t, 0, "T");
    dart_flutter_add_method(registry, arena, "flutter.lib.material.NavigatorState", "pop", void_t,
                            0, "T");
    dart_flutter_add_method(registry, arena, "flutter.lib.material.NavigatorState", "canPop",
                            bool_t, 0, NULL);

    dart_flutter_add_method(registry, arena, "flutter.lib.material.Scaffold", "of",
                            cbm_type_named(arena, "flutter.lib.material.ScaffoldState"),
                            CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.ScaffoldState", "openDrawer",
                            void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.ScaffoldState", "closeDrawer",
                            void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.Theme", "of", theme_data_t,
                            CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.ThemeData", "copyWith",
                            theme_data_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.MediaQuery", "of", media_data_t,
                            CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.MediaQueryData", "size",
                            cbm_type_named(arena, "flutter.lib.material.Size"),
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.Size", "width", double_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.Size", "height", double_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);

    dart_flutter_add_method(registry, arena, "flutter.lib.material.ChangeNotifier",
                            "notifyListeners", void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.ChangeNotifier", "addListener",
                            void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.ChangeNotifier",
                            "removeListener", void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.ChangeNotifier", "dispose",
                            void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.TextEditingController", "text",
                            string_t, CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.TextEditingController", "clear",
                            void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.ScrollController", "animateTo",
                            dart_flutter_t1(arena, "dart.async.Future", void_t), 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.ScrollController", "jumpTo",
                            void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.ScrollController", "offset",
                            double_t, CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.AnimationController", "forward",
                            cbm_type_named(arena, "flutter.lib.material.TickerFuture"), 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.AnimationController", "reverse",
                            cbm_type_named(arena, "flutter.lib.material.TickerFuture"), 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.AnimationController", "reset",
                            void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.AnimationController", "stop",
                            void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter.lib.material.AnimationController", "dispose",
                            void_t, 0, NULL);

    static const char *const color_names[] = {"red",     "pink",  "purple", "blue",
                                              "green",   "yellow", "orange", "grey",
                                              "black",   "white", "transparent", NULL};
    for (int i = 0; color_names[i]; i++) {
        dart_flutter_add_method(registry, arena, "flutter.lib.material.Colors", color_names[i],
                                color_t, CBM_FUNC_FLAG_PROPERTY | CBM_FUNC_FLAG_STATICMETHOD, NULL);
    }

    static const char *const concrete_widgets[] = {
        "Scaffold", "AppBar", "Text", "Container", "Row", "Column", "ListView",
        "GestureDetector", "TextEditingController", "ScrollController", "AnimationController",
        "Theme", "ThemeData", "MediaQuery", "Color", "MaterialApp", "TextStyle", "ValueKey",
        "GlobalKey", "MaterialPageRoute", NULL};
    for (int i = 0; concrete_widgets[i]; i++) {
        const char *qn = cbm_arena_sprintf(arena, "%s.%s", lib, concrete_widgets[i]);
        dart_flutter_add_constructor(registry, arena, qn, NULL, cbm_type_named(arena, qn), false);
    }
    dart_flutter_add_constructor(registry, arena, "flutter.lib.material.ListView", "builder",
                                 cbm_type_named(arena, "flutter.lib.material.ListView"), false);
    dart_flutter_add_constructor(registry, arena, "flutter.lib.material.ListView", "separated",
                                 cbm_type_named(arena, "flutter.lib.material.ListView"), false);
    dart_flutter_add_constructor(registry, arena, "flutter.lib.material.EdgeInsets", "all",
                                 cbm_type_named(arena, "flutter.lib.material.EdgeInsets"), true);
    dart_flutter_add_constructor(registry, arena, "flutter.lib.material.EdgeInsets", "symmetric",
                                 cbm_type_named(arena, "flutter.lib.material.EdgeInsets"), true);
    dart_flutter_add_constructor(registry, arena, "flutter.lib.material.EdgeInsets", "only",
                                 cbm_type_named(arena, "flutter.lib.material.EdgeInsets"), true);
    dart_flutter_add_constructor(registry, arena, "flutter.lib.material.EdgeInsets", "fromLTRB",
                                 cbm_type_named(arena, "flutter.lib.material.EdgeInsets"), false);
    dart_flutter_add_function(registry, arena, lib, "showDialog", future_t, "T");
    dart_flutter_add_function(registry, arena, lib, "showModalBottomSheet", future_t, "T");
    dart_flutter_add_function(registry, arena, lib, "runApp", void_t, NULL);

    (void)int_t;
}

static void dart_flutter_register_http(CBMTypeRegistry *registry, CBMArena *arena) {
    const char *lib = "http.lib.http";
    const CBMType *string_t = cbm_type_named(arena, "dart.core.String");
    const CBMType *int_t = cbm_type_named(arena, "dart.core.int");
    const CBMType *bool_t = cbm_type_named(arena, "dart.core.bool");
    const CBMType *response_t = cbm_type_named(arena, "http.lib.http.Response");
    const CBMType *streamed_t = cbm_type_named(arena, "http.lib.http.StreamedResponse");
    const CBMType *future_response = dart_flutter_t1(arena, "dart.async.Future", response_t);
    dart_flutter_add_type(registry, arena, "http.lib.http.BaseClient", "dart.core.Object", NULL,
                          true);
    dart_flutter_add_type(registry, arena, "http.lib.http.Client", "http.lib.http.BaseClient", NULL,
                          false);
    dart_flutter_add_type(registry, arena, "http.lib.http.Response", "dart.core.Object", NULL,
                          false);
    dart_flutter_add_type(registry, arena, "http.lib.http.StreamedResponse", "dart.core.Object",
                          NULL, false);
    dart_flutter_add_type(registry, arena, "http.lib.http.Request", "dart.core.Object", NULL,
                          false);
    static const char *const verbs[] = {"get", "post", "put", "patch", "delete", "head", NULL};
    for (int i = 0; verbs[i]; i++) {
        dart_flutter_add_method(registry, arena, "http.lib.http.BaseClient", verbs[i],
                                future_response, 0, NULL);
        dart_flutter_add_function(registry, arena, lib, verbs[i], future_response, NULL);
    }
    dart_flutter_add_method(registry, arena, "http.lib.http.BaseClient", "send",
                            dart_flutter_t1(arena, "dart.async.Future", streamed_t), 0, NULL);
    dart_flutter_add_method(registry, arena, "http.lib.http.BaseClient", "close",
                            cbm_type_named(arena, "dart.core.void"), 0, NULL);
    dart_flutter_add_method(registry, arena, "http.lib.http.Response", "body", string_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "http.lib.http.Response", "bodyBytes",
                            cbm_type_named(arena, "dart.typed_data.Uint8List"),
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "http.lib.http.Response", "statusCode", int_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "http.lib.http.Response", "isRedirect", bool_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_constructor(registry, arena, "http.lib.http.Client", NULL,
                                 cbm_type_named(arena, "http.lib.http.Client"), true);
    dart_flutter_add_constructor(registry, arena, "http.lib.http.Request", NULL,
                                 cbm_type_named(arena, "http.lib.http.Request"), false);
}

static void dart_flutter_register_dio(CBMTypeRegistry *registry, CBMArena *arena) {
    const CBMType *void_t = cbm_type_named(arena, "dart.core.void");
    const CBMType *t_t = cbm_type_type_param(arena, "T");
    const CBMType *response_t = dart_flutter_t1(arena, "dio.lib.dio.Response", t_t);
    const CBMType *future_response = dart_flutter_t1(arena, "dart.async.Future", response_t);
    dart_flutter_add_type(registry, arena, "dio.lib.dio.Dio", "dart.core.Object", NULL, false);
    dart_flutter_add_type(registry, arena, "dio.lib.dio.Response", "dart.core.Object", "T", false);
    dart_flutter_add_type(registry, arena, "dio.lib.dio.Options", "dart.core.Object", NULL, false);
    dart_flutter_add_type(registry, arena, "dio.lib.dio.BaseOptions", "dart.core.Object", NULL,
                          false);
    dart_flutter_add_type(registry, arena, "dio.lib.dio.Interceptors", "dart.core.Object", NULL,
                          false);
    dart_flutter_add_type(registry, arena, "dio.lib.dio.Interceptor", "dart.core.Object", NULL,
                          true);
    static const char *const verbs[] = {"get", "post", "put", "patch", "delete", "request",
                                        "download", NULL};
    for (int i = 0; verbs[i]; i++) {
        dart_flutter_add_method(registry, arena, "dio.lib.dio.Dio", verbs[i], future_response, 0,
                                "T");
    }
    dart_flutter_add_method(registry, arena, "dio.lib.dio.Dio", "close", void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "dio.lib.dio.Dio", "options",
                            cbm_type_named(arena, "dio.lib.dio.BaseOptions"),
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "dio.lib.dio.Dio", "interceptors",
                            cbm_type_named(arena, "dio.lib.dio.Interceptors"),
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "dio.lib.dio.Response", "data", t_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "dio.lib.dio.Response", "statusCode",
                            cbm_type_named(arena, "dart.core.int"), CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "dio.lib.dio.Interceptors", "add", void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "dio.lib.dio.Interceptors", "clear", void_t, 0,
                            NULL);
    dart_flutter_add_constructor(registry, arena, "dio.lib.dio.Dio", NULL,
                                 cbm_type_named(arena, "dio.lib.dio.Dio"), false);
    dart_flutter_add_constructor(registry, arena, "dio.lib.dio.Options", NULL,
                                 cbm_type_named(arena, "dio.lib.dio.Options"), false);
    dart_flutter_add_constructor(registry, arena, "dio.lib.dio.BaseOptions", NULL,
                                 cbm_type_named(arena, "dio.lib.dio.BaseOptions"), false);
}

static void dart_flutter_register_state_packages(CBMTypeRegistry *registry, CBMArena *arena) {
    const CBMType *void_t = cbm_type_named(arena, "dart.core.void");
    const CBMType *t_t = cbm_type_type_param(arena, "T");
    const CBMType *s_t = cbm_type_type_param(arena, "S");
    const CBMType *widget_t = cbm_type_named(arena, "flutter.lib.material.Widget");

    dart_flutter_add_type(registry, arena, "provider.lib.provider.Provider", "dart.core.Object", "T",
                          false);
    dart_flutter_add_type(registry, arena, "provider.lib.provider.Consumer",
                          "flutter.lib.material.StatelessWidget", "T", false);
    dart_flutter_add_type(registry, arena, "provider.lib.provider.ChangeNotifierProvider",
                          "provider.lib.provider.Provider", "T", false);
    dart_flutter_add_method(registry, arena, "provider.lib.provider.Provider", "of", t_t,
                            CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_flutter_add_constructor(registry, arena, "provider.lib.provider.Provider", NULL,
                                 cbm_type_named(arena, "provider.lib.provider.Provider"), false);
    dart_flutter_add_constructor(registry, arena, "provider.lib.provider.Consumer", NULL,
                                 cbm_type_named(arena, "provider.lib.provider.Consumer"), false);
    dart_flutter_add_constructor(registry, arena, "provider.lib.provider.ChangeNotifierProvider",
                                 NULL,
                                 cbm_type_named(arena,
                                                "provider.lib.provider.ChangeNotifierProvider"),
                                 false);

    dart_flutter_add_type(registry, arena, "flutter_riverpod.lib.flutter_riverpod.WidgetRef",
                          "dart.core.Object", NULL, true);
    dart_flutter_add_type(registry, arena, "flutter_riverpod.lib.flutter_riverpod.Ref",
                          "dart.core.Object", NULL, true);
    dart_flutter_add_type(registry, arena, "flutter_riverpod.lib.flutter_riverpod.ConsumerWidget",
                          "flutter.lib.material.StatelessWidget", NULL, true);
    dart_flutter_add_type(registry, arena,
                          "flutter_riverpod.lib.flutter_riverpod.ConsumerStatefulWidget",
                          "flutter.lib.material.StatefulWidget", NULL, true);
    dart_flutter_add_type(registry, arena, "flutter_riverpod.lib.flutter_riverpod.ConsumerState",
                          "flutter.lib.material.State", "T", true);
    dart_flutter_add_type(registry, arena, "flutter_riverpod.lib.flutter_riverpod.Provider",
                          "dart.core.Object", "T", false);
    dart_flutter_add_method(registry, arena, "flutter_riverpod.lib.flutter_riverpod.WidgetRef",
                            "watch", t_t, 0, "T");
    dart_flutter_add_method(registry, arena, "flutter_riverpod.lib.flutter_riverpod.WidgetRef",
                            "read", t_t, 0, "T");
    dart_flutter_add_method(registry, arena, "flutter_riverpod.lib.flutter_riverpod.Ref", "watch",
                            t_t, 0, "T");
    dart_flutter_add_method(registry, arena, "flutter_riverpod.lib.flutter_riverpod.Ref", "read",
                            t_t, 0, "T");
    dart_flutter_add_method(registry, arena,
                            "flutter_riverpod.lib.flutter_riverpod.ConsumerWidget", "build",
                            widget_t, 0, NULL);
    dart_flutter_add_constructor(registry, arena,
                                 "flutter_riverpod.lib.flutter_riverpod.Provider", NULL,
                                 cbm_type_named(
                                     arena, "flutter_riverpod.lib.flutter_riverpod.Provider"),
                                 false);

    dart_flutter_add_type(registry, arena, "flutter_bloc.lib.flutter_bloc.BlocBase",
                          "dart.core.Object", "S", true);
    dart_flutter_add_type(registry, arena, "flutter_bloc.lib.flutter_bloc.Cubit",
                          "flutter_bloc.lib.flutter_bloc.BlocBase", "S", false);
    dart_flutter_add_type(registry, arena, "flutter_bloc.lib.flutter_bloc.Bloc",
                          "flutter_bloc.lib.flutter_bloc.BlocBase", "S", false);
    dart_flutter_add_type(registry, arena, "flutter_bloc.lib.flutter_bloc.BlocProvider",
                          "flutter.lib.material.StatelessWidget", "T", false);
    dart_flutter_add_type(registry, arena, "flutter_bloc.lib.flutter_bloc.BlocBuilder",
                          "flutter.lib.material.StatefulWidget", "T", false);
    dart_flutter_add_method(registry, arena, "flutter_bloc.lib.flutter_bloc.BlocBase", "emit",
                            void_t, 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter_bloc.lib.flutter_bloc.BlocBase", "state", s_t,
                            CBM_FUNC_FLAG_PROPERTY, NULL);
    dart_flutter_add_method(registry, arena, "flutter_bloc.lib.flutter_bloc.BlocBase", "close",
                            dart_flutter_t1(arena, "dart.async.Future", void_t), 0, NULL);
    dart_flutter_add_method(registry, arena, "flutter_bloc.lib.flutter_bloc.Bloc", "add", void_t, 0,
                            NULL);
    dart_flutter_add_method(registry, arena, "flutter_bloc.lib.flutter_bloc.BlocProvider", "of",
                            t_t, CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_flutter_add_constructor(registry, arena, "flutter_bloc.lib.flutter_bloc.Cubit", NULL,
                                 cbm_type_named(arena, "flutter_bloc.lib.flutter_bloc.Cubit"),
                                 false);
    dart_flutter_add_constructor(registry, arena, "flutter_bloc.lib.flutter_bloc.BlocProvider",
                                 NULL,
                                 cbm_type_named(arena, "flutter_bloc.lib.flutter_bloc.BlocProvider"),
                                 false);
    dart_flutter_add_constructor(registry, arena, "flutter_bloc.lib.flutter_bloc.BlocBuilder", NULL,
                                 cbm_type_named(arena, "flutter_bloc.lib.flutter_bloc.BlocBuilder"),
                                 false);
}

static void dart_flutter_register_services(CBMTypeRegistry *registry, CBMArena *arena) {
    const CBMType *void_t = cbm_type_named(arena, "dart.core.void");
    const CBMType *bool_t = cbm_type_named(arena, "dart.core.bool");
    const CBMType *int_t = cbm_type_named(arena, "dart.core.int");
    const CBMType *double_t = cbm_type_named(arena, "dart.core.double");
    const CBMType *string_t = cbm_type_named(arena, "dart.core.String");
    const CBMType *t_t = cbm_type_type_param(arena, "T");
    const CBMType *get_it_t = cbm_type_named(arena, "get_it.lib.get_it.GetIt");

    dart_flutter_add_type(registry, arena, "get_it.lib.get_it.GetIt", "dart.core.Object", NULL,
                          false);
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "instance", get_it_t,
                            CBM_FUNC_FLAG_PROPERTY | CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "I", get_it_t,
                            CBM_FUNC_FLAG_PROPERTY | CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "get", t_t, 0, "T");
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "call", t_t, 0, "T");
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "registerSingleton", t_t,
                            0, "T");
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "registerLazySingleton",
                            void_t, 0, "T");
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "registerFactory", void_t,
                            0, "T");
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "isRegistered", bool_t, 0,
                            "T");
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "unregister",
                            dart_flutter_t1(arena, "dart.async.Future", void_t), 0, "T");
    dart_flutter_add_method(registry, arena, "get_it.lib.get_it.GetIt", "reset",
                            dart_flutter_t1(arena, "dart.async.Future", void_t), 0, NULL);

    const char *prefs = "shared_preferences.lib.shared_preferences.SharedPreferences";
    const CBMType *prefs_t = cbm_type_named(arena, prefs);
    dart_flutter_add_type(registry, arena, prefs, "dart.core.Object", NULL, false);
    dart_flutter_add_method(registry, arena, prefs, "getInstance",
                            dart_flutter_t1(arena, "dart.async.Future", prefs_t),
                            CBM_FUNC_FLAG_STATICMETHOD, NULL);
    dart_flutter_add_method(registry, arena, prefs, "getString", string_t, 0, NULL);
    dart_flutter_add_method(registry, arena, prefs, "getInt", int_t, 0, NULL);
    dart_flutter_add_method(registry, arena, prefs, "getDouble", double_t, 0, NULL);
    dart_flutter_add_method(registry, arena, prefs, "getBool", bool_t, 0, NULL);
    dart_flutter_add_method(registry, arena, prefs, "getStringList",
                            dart_flutter_t1(arena, "dart.core.List", string_t), 0, NULL);
    dart_flutter_add_method(registry, arena, prefs, "containsKey", bool_t, 0, NULL);
    static const char *const setters[] = {"setString", "setInt", "setDouble", "setBool",
                                          "setStringList", "remove", "clear", NULL};
    for (int i = 0; setters[i]; i++) {
        dart_flutter_add_method(registry, arena, prefs, setters[i],
                                dart_flutter_t1(arena, "dart.async.Future", bool_t), 0, NULL);
    }
}

void cbm_dart_flutter_seed_register(CBMTypeRegistry *registry, CBMArena *arena) {
    dart_flutter_register_framework(registry, arena);
    dart_flutter_register_http(registry, arena);
    dart_flutter_register_dio(registry, arena);
    dart_flutter_register_state_packages(registry, arena);
    dart_flutter_register_services(registry, arena);
}
