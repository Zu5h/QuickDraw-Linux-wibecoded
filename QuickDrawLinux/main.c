#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define DBG(fmt, ...) g_printerr("[QD] " fmt "\n", ##__VA_ARGS__)

typedef struct {
    GtkWidget *window;
    WebKitWebView *web_view;
    gchar *web_src_dir;
} QuickDrawApp;

static GHashTable *folder_index_map = NULL;
static GHashTable *folder_real_paths = NULL;

static gchar *resolve_qd_uri(const gchar *uri);

static gchar *get_websrc_path(QuickDrawApp *app, const gchar *filename) {
    return g_build_filename(app->web_src_dir, filename, NULL);
}

static void run_js(WebKitWebView *web_view, const gchar *script) {
    DBG("run_js: %s", script);
    webkit_web_view_evaluate_javascript(web_view, script, -1, NULL, NULL, NULL, NULL, NULL);
}

static void post_message_to_js(WebKitWebView *web_view, const gchar *type, const gchar *json_data) {
    gchar *js = g_strdup_printf(
        "window.dispatchEvent(new CustomEvent('qd-message', {detail: {type:'%s',data:%s}}));",
        type, json_data ? json_data : "null"
    );
    run_js(web_view, js);
    g_free(js);
}

static gboolean is_image_file(const gchar *filename) {
    const gchar *ext = strrchr(filename, '.');
    if (!ext) return FALSE;
    ext++;
    return (g_ascii_strcasecmp(ext, "jpg") == 0 ||
            g_ascii_strcasecmp(ext, "jpeg") == 0 ||
            g_ascii_strcasecmp(ext, "png") == 0);
}

static GList *get_folder_images(const gchar *path) {
    GList *images = NULL;
    DIR *dir = opendir(path);
    if (!dir) {
        DBG("opendir failed: %s", path);
        return NULL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        gchar *full_path = g_build_filename(path, entry->d_name, NULL);
        struct stat st;
        if (stat(full_path, &st) != 0) {
            g_free(full_path);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            GList *sub = get_folder_images(full_path);
            images = g_list_concat(images, sub);
        } else if (S_ISREG(st.st_mode) && is_image_file(entry->d_name)) {
            images = g_list_prepend(images, full_path);
        } else {
            g_free(full_path);
        }
    }
    closedir(dir);
    return images;
}

static gint count_folder_images(const gchar *path) {
    GList *images = get_folder_images(path);
    gint count = (gint)g_list_length(images);
    g_list_free_full(images, g_free);
    return count;
}

static void open_folder_in_filemanager(const gchar *path) {
    DBG("open_folder: %s", path);
    gchar *cmd = g_strdup_printf("xdg-open \"%s\"", path);
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
}

static void open_image_in_filemanager(const gchar *url) {
    DBG("open_image: %s", url);
    gchar *real_path = resolve_qd_uri(url);
    if (real_path) {
        gchar *dir = g_path_get_dirname(real_path);
        open_folder_in_filemanager(dir);
        g_free(dir);
        g_free(real_path);
    }
}

static void build_folders_json(JsonBuilder *builder, GSList *paths) {
    json_builder_begin_array(builder);
    for (GSList *iter = paths; iter; iter = iter->next) {
        const gchar *path = (const gchar *)iter->data;
        gint count = count_folder_images(path);
        DBG("folder: %s -> %d images", path, count);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "Path");
        json_builder_add_string_value(builder, path);
        json_builder_set_member_name(builder, "Count");
        json_builder_add_int_value(builder, count);
        json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
}

static void load_uri(QuickDrawApp *app, const gchar *filename) {
    gchar *filepath = get_websrc_path(app, filename);
    gchar *uri = g_filename_to_uri(filepath, NULL, NULL);
    DBG("load_uri: filepath=%s  uri=%s", filepath, uri ? uri : "(NULL)");
    if (uri) {
        webkit_web_view_load_uri(app->web_view, uri);
        g_free(uri);
    } else {
        g_printerr("[QD] ERROR: g_filename_to_uri returned NULL for '%s'\n", filepath);
    }
    g_free(filepath);
}

static void on_add_folders(QuickDrawApp *app) {
    DBG("on_add_folders");
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Choose Directories",
        GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL
    );
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList *filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        if (filenames) {
            JsonBuilder *builder = json_builder_new();
            build_folders_json(builder, filenames);
            JsonNode *root = json_builder_get_root(builder);
            gchar *json = json_to_string(root, FALSE);
            post_message_to_js(app->web_view, "UpdateFolders", json);
            g_free(json);
            g_object_unref(builder);
            g_slist_free_full(filenames, g_free);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_refresh_folder(QuickDrawApp *app, const gchar *path) {
    DBG("on_refresh_folder: %s", path);
    gint count = count_folder_images(path);
    if (count > 0) {
        JsonBuilder *builder = json_builder_new();
        json_builder_begin_array(builder);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "Path");
        json_builder_add_string_value(builder, path);
        json_builder_set_member_name(builder, "Count");
        json_builder_add_int_value(builder, count);
        json_builder_end_object(builder);
        json_builder_end_array(builder);
        JsonNode *root = json_builder_get_root(builder);
        gchar *json = json_to_string(root, FALSE);
        post_message_to_js(app->web_view, "UpdateFolders", json);
        g_free(json);
        g_object_unref(builder);
    }
}

static void on_refresh_folders(QuickDrawApp *app, JsonArray *paths) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);
    guint len = json_array_get_length(paths);
    for (guint i = 0; i < len; i++) {
        const gchar *path = json_array_get_string_element(paths, i);
        gint count = count_folder_images(path);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "Path");
        json_builder_add_string_value(builder, path);
        json_builder_set_member_name(builder, "Count");
        json_builder_add_int_value(builder, count);
        json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
    JsonNode *root = json_builder_get_root(builder);
    gchar *json = json_to_string(root, FALSE);
    post_message_to_js(app->web_view, "UpdateFolders", json);
    g_free(json);
    g_object_unref(builder);
}

static gchar *resolve_qd_uri(const gchar *uri) {
    if (!g_str_has_prefix(uri, "qd://")) return NULL;
    const gchar *path_part = uri + strlen("qd://");
    gchar **parts = g_strsplit(path_part, "/", 2);
    if (!parts || !parts[0] || !parts[1]) {
        g_strfreev(parts);
        return NULL;
    }
    gint idx = atoi(parts[0]);
    const gchar *real_dir = g_hash_table_lookup(folder_real_paths, GINT_TO_POINTER(idx));
    if (!real_dir) {
        g_strfreev(parts);
        return NULL;
    }
    gchar *decoded_name = g_uri_unescape_string(parts[1], NULL);
    gchar *full_path = g_build_filename(real_dir, decoded_name, NULL);
    g_free(decoded_name);
    g_strfreev(parts);
    return full_path;
}

static void qd_scheme_request_callback(WebKitURISchemeRequest *request, gpointer user_data) {
    const gchar *uri = webkit_uri_scheme_request_get_uri(request);
    DBG("qd scheme request: %s", uri);

    gchar *real_path = resolve_qd_uri(uri);
    if (!real_path || !g_file_test(real_path, G_FILE_TEST_IS_REGULAR)) {
        DBG("  file not found: %s", real_path ? real_path : "(null)");
        GError *err = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File not found: %s", uri);
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        g_free(real_path);
        return;
    }

    gchar *contents = NULL;
    gsize length = 0;
    GError *err = NULL;

    if (!g_file_get_contents(real_path, &contents, &length, &err)) {
        DBG("  read error: %s", err->message);
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        g_free(real_path);
        return;
    }

    const gchar *content_type = "application/octet-stream";
    const gchar *ext = strrchr(real_path, '.');
    if (ext) {
        if (g_ascii_strcasecmp(ext, ".jpg") == 0 || g_ascii_strcasecmp(ext, ".jpeg") == 0)
            content_type = "image/jpeg";
        else if (g_ascii_strcasecmp(ext, ".png") == 0)
            content_type = "image/png";
    }

    GInputStream *stream = g_memory_input_stream_new_from_data(contents, length, g_free);
    WebKitURISchemeResponse *response = webkit_uri_scheme_response_new(stream, (gint64)length);
    webkit_uri_scheme_response_set_content_type(response, content_type);
    webkit_uri_scheme_request_finish_with_response(request, response);
    g_object_unref(response);
    g_object_unref(stream);
    g_free(real_path);
}

static void on_get_images(QuickDrawApp *app, JsonArray *paths, gint interval) {
    DBG("on_get_images: interval=%d", interval);
    if (folder_index_map) g_hash_table_destroy(folder_index_map);
    folder_index_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    if (folder_real_paths) g_hash_table_destroy(folder_real_paths);
    folder_real_paths = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);

    GHashTable *all_images = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    guint len = json_array_get_length(paths);
    for (guint i = 0; i < len; i++) {
        const gchar *path = json_array_get_string_element(paths, i);
        g_hash_table_insert(folder_index_map, GINT_TO_POINTER(i), g_strdup(path));
        g_hash_table_insert(folder_real_paths, GINT_TO_POINTER(i), g_strdup(path));

        GList *images = get_folder_images(path);
        gint img_count = g_list_length(images);
        DBG("  folder[%d]: %s -> %d images", i, path, img_count);
        gsize path_prefix_len = strlen(path);
        for (GList *iter = images; iter; iter = iter->next) {
            gchar *full_path = (gchar *)iter->data;
            const gchar *rel_path = full_path + path_prefix_len;
            if (*rel_path == G_DIR_SEPARATOR) rel_path++;
            gchar *encoded = g_uri_escape_string(rel_path, "/$", FALSE);
            gchar *image_url = g_strdup_printf("qd://%d/%s", i, encoded);
            g_hash_table_add(all_images, image_url);
            g_free(encoded);
        }
        g_list_free_full(images, g_free);
    }

    GList *image_list = g_hash_table_get_keys(all_images);
    DBG("total images: %d", g_list_length(image_list));
    if (!image_list) {
        GtkWidget *msg = gtk_message_dialog_new(
            GTK_WINDOW(app->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "No images found! Select one or more folders."
        );
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
        g_hash_table_destroy(all_images);
        return;
    }

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "interval");
    json_builder_add_int_value(builder, (gint64)interval * 1000);
    json_builder_set_member_name(builder, "images");
    json_builder_begin_array(builder);
    for (GList *iter = image_list; iter; iter = iter->next)
        json_builder_add_string_value(builder, (gchar *)iter->data);
    json_builder_end_array(builder);
    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);
    gchar *json_str = json_to_string(root, FALSE);

    gchar *js_source = g_strdup_printf("var slideshowData = %s;", json_str);
    WebKitUserContentManager *mgr = webkit_web_view_get_user_content_manager(app->web_view);
    WebKitUserScript *slideshow_script = webkit_user_script_new(
        js_source,
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, 0
    );
    webkit_user_content_manager_add_script(mgr, slideshow_script);
    webkit_user_script_unref(slideshow_script);
    g_free(js_source);
    g_free(json_str);
    g_object_unref(builder);

    load_uri(app, "slideshow.html");

    g_hash_table_destroy(all_images);
    g_list_free(image_list);
}

static void on_stop_slideshow(QuickDrawApp *app) {
    DBG("on_stop_slideshow");
    WebKitUserContentManager *mgr = webkit_web_view_get_user_content_manager(app->web_view);
    webkit_user_content_manager_remove_all_scripts(mgr);

    WebKitUserScript *init_script = webkit_user_script_new(
        "window.QuickDrawLinux = true;"
        "console.log('[QD] init script injected, QuickDrawLinux =', window.QuickDrawLinux);",
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, 0
    );
    webkit_user_content_manager_add_script(mgr, init_script);
    webkit_user_script_unref(init_script);

    load_uri(app, "index.html");
}

static void web_view_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data G_GNUC_UNUSED) {
    const gchar *uri = webkit_web_view_get_uri(web_view);
    switch (event) {
        case WEBKIT_LOAD_STARTED:
            DBG("LOAD_STARTED: %s", uri);
            break;
        case WEBKIT_LOAD_REDIRECTED:
            DBG("LOAD_REDIRECTED: %s", uri);
            break;
        case WEBKIT_LOAD_COMMITTED:
            DBG("LOAD_COMMITTED: %s", uri);
            break;
        case WEBKIT_LOAD_FINISHED:
            DBG("LOAD_FINISHED: %s", uri);
            break;
    }
}

static gboolean script_message_callback(WebKitUserContentManager *manager G_GNUC_UNUSED,
                                         WebKitJavascriptResult *result,
                                         gpointer user_data) {
    QuickDrawApp *app = (QuickDrawApp *)user_data;
    DBG("script_message_callback fired");
    JSCValue *value = webkit_javascript_result_get_js_value(result);
    if (!jsc_value_is_object(value)) {
        DBG("  result is not an object");
        return TRUE;
    }

    gchar *json_str = jsc_value_to_json(value, 0);
    if (!json_str) {
        DBG("  jsc_value_to_json returned NULL");
        return TRUE;
    }
    DBG("  raw JSON: %s", json_str);

    JsonParser *parser = json_parser_new();
    GError *err = NULL;
    if (!json_parser_load_from_data(parser, json_str, -1, &err)) {
        g_printerr("[QD] JSON parse error: %s\n", err->message);
        g_error_free(err);
        g_object_unref(parser);
        g_free(json_str);
        return TRUE;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        g_free(json_str);
        return TRUE;
    }
    JsonObject *obj = json_node_get_object(root);
    const gchar *type = json_object_get_string_member(obj, "type");
    DBG("  message type: %s", type);

    if (g_strcmp0(type, "addFolders") == 0) {
        on_add_folders(app);
    } else if (g_strcmp0(type, "refreshFolder") == 0) {
        const gchar *path = json_object_get_string_member(obj, "path");
        if (path) on_refresh_folder(app, path);
    } else if (g_strcmp0(type, "refreshFolders") == 0) {
        JsonArray *paths = json_object_get_array_member(obj, "paths");
        if (paths) on_refresh_folders(app, paths);
    } else if (g_strcmp0(type, "openFolder") == 0) {
        const gchar *path = json_object_get_string_member(obj, "path");
        if (path) open_folder_in_filemanager(path);
    } else if (g_strcmp0(type, "getImages") == 0) {
        JsonArray *paths = json_object_get_array_member(obj, "paths");
        gint interval = json_object_get_int_member(obj, "interval");
        if (paths) on_get_images(app, paths, interval);
    } else if (g_strcmp0(type, "openImage") == 0) {
        const gchar *path = json_object_get_string_member(obj, "path");
        if (path) open_image_in_filemanager(path);
    } else if (g_strcmp0(type, "stopSlideshow") == 0) {
        on_stop_slideshow(app);
    }

    g_object_unref(parser);
    g_free(json_str);
    return TRUE;
}

static WebKitUserScript *create_init_script(void) {
    return webkit_user_script_new(
        "window.QuickDrawLinux = true;"
        "console.log('[QD] init script injected, QuickDrawLinux =', window.QuickDrawLinux);",
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, 0
    );
}

static gchar *find_websrc_dir(void) {
    gchar exe_path[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) exe_path[len] = '\0';
    else strcpy(exe_path, ".");

    gchar *exe_dir = g_path_get_dirname(exe_path);
    gchar *exe_dir_parent = g_build_filename(exe_dir, "..", NULL);
    DBG("exe_path: %s", exe_path);
    DBG("exe_dir: %s", exe_dir);

    gchar *candidates[] = {
        g_build_filename(exe_dir, "..", "WebSrc", NULL),
        g_build_filename(exe_dir, "WebSrc", NULL),
        g_build_filename(exe_dir, "..", "share", "quickdraw", "WebSrc", NULL),
        g_build_filename("..", "WebSrc", NULL),
        g_build_filename("WebSrc", NULL),
        NULL
    };

    gchar *result = NULL;
    for (int i = 0; candidates[i]; i++) {
        gchar *canonical = g_canonicalize_filename(candidates[i], NULL);
        g_free(candidates[i]);
        candidates[i] = NULL;
        DBG("candidate[%d]: %s (exists=%d)", i, canonical,
            g_file_test(canonical, G_FILE_TEST_IS_DIR));
        if (!result && g_file_test(canonical, G_FILE_TEST_IS_DIR)) {
            result = canonical;
        } else {
            g_free(canonical);
        }
    }

    g_free(exe_dir);
    g_free(exe_dir_parent);
    DBG("web_src_dir: %s", result ? result : "(NOT FOUND)");
    return result;
}

static void activate(GtkApplication *gtk_app, gpointer user_data G_GNUC_UNUSED) {
    DBG("=== activate ===");
    QuickDrawApp *app = g_new0(QuickDrawApp, 1);
    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "QuickDraw");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 800, 500);
    gtk_window_set_position(GTK_WINDOW(app->window), GTK_WIN_POS_CENTER);

    app->web_src_dir = find_websrc_dir();
    if (!app->web_src_dir) {
        GtkWidget *msg = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Cannot find WebSrc directory!");
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
        g_free(app);
        return;
    }

    gchar *icon_candidates[] = {
        g_build_filename(app->web_src_dir, "..", "QuickDrawLinux", "QuickDraw.ico", NULL),
        g_build_filename(app->web_src_dir, "..", "..", "QuickDraw.ico", NULL),
        g_build_filename(app->web_src_dir, "..", "..", "icons", "hicolor", "256x256", "apps", "quickdraw.png", NULL),
        NULL
    };
    for (int i = 0; icon_candidates[i]; i++) {
        gchar *c = g_canonicalize_filename(icon_candidates[i], NULL);
        g_free(icon_candidates[i]);
        if (g_file_test(c, G_FILE_TEST_IS_REGULAR)) {
            gtk_window_set_icon_from_file(GTK_WINDOW(app->window), c, NULL);
            gtk_window_set_default_icon_from_file(c, NULL);
            g_free(c);
            break;
        }
        g_free(c);
    }

    WebKitSettings *settings = webkit_settings_new();
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, TRUE);

    app->web_view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_settings(settings));

    WebKitWebContext *ctx = webkit_web_context_get_default();
    webkit_web_context_register_uri_scheme(ctx, "qd",
        (WebKitURISchemeRequestCallback)qd_scheme_request_callback, NULL, NULL);

    g_signal_connect(app->web_view, "load-changed", G_CALLBACK(web_view_load_changed), NULL);

    WebKitUserContentManager *content_mgr = webkit_web_view_get_user_content_manager(app->web_view);
    WebKitUserScript *init_script = create_init_script();
    webkit_user_content_manager_add_script(content_mgr, init_script);
    webkit_user_script_unref(init_script);

    webkit_user_content_manager_register_script_message_handler(content_mgr, "bridge");
    g_signal_connect(content_mgr, "script-message-received::bridge",
                     G_CALLBACK(script_message_callback), app);

    load_uri(app, "index.html");

    gtk_container_add(GTK_CONTAINER(app->window), GTK_WIDGET(app->web_view));
    gtk_widget_show_all(app->window);

    g_object_unref(settings);
    DBG("activate done");
}

int main(int argc, char *argv[]) {
    gchar exe_path[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) exe_path[len] = '\0';
    else strcpy(exe_path, ".");

    gchar *exe_dir = g_path_get_dirname(exe_path);
    gchar *share_dir = g_build_filename(exe_dir, "..", "share", NULL);
    gchar *share_canon = g_canonicalize_filename(share_dir, NULL);
    g_free(share_dir);

    const gchar *old_xdg = g_getenv("XDG_DATA_DIRS");
    gchar *new_xdg;
    if (old_xdg && old_xdg[0]) {
        new_xdg = g_strdup_printf("%s:%s", share_canon, old_xdg);
    } else {
        new_xdg = g_strdup_printf("%s:/usr/local/share:/usr/share", share_canon);
    }
    g_setenv("XDG_DATA_DIRS", new_xdg, TRUE);
    g_free(new_xdg);
    g_free(share_canon);
    g_free(exe_dir);

    DBG("QuickDraw Linux starting...");
    GtkApplication *gtk_app = gtk_application_new("com.mfdigitalmedia.quickdraw",
                                                   G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
