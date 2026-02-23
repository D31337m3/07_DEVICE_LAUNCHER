#include "services/fileserver_service.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "services/sdcard_service.h"
#include "services/storage_service.h"

static const char *TAG = "fileserver";

static httpd_handle_t s_httpd = nullptr;
static bool s_running = false;

static wifi_mode_t s_prev_mode = WIFI_MODE_NULL;
static wifi_config_t s_prev_sta_cfg = {};
static bool s_prev_sta_cfg_valid = false;

static esp_netif_t *s_ap_netif = nullptr;

static char s_ap_ssid[33] = "DeviceLauncherFS";
static const char *kApIp = "192.168.4.1";

static const char kIndexHtml[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 FileServer</title>"
    "<style>body{font-family:sans-serif;margin:16px}"
    "textarea{width:100%;height:45vh}"
    "input,select,button{font-size:16px;margin:4px 0}"
    "pre{background:#f4f4f4;padding:8px;overflow:auto}"
    "</style></head><body>"
    "<h2>FileServer</h2>"
    "<div>Root: <select id='root'><option value='flash'>flash (/storage)</option><option value='sd'>sd (/sdcard)</option></select></div>"
    "<div>Path: <input id='path' placeholder='e.g. files/test.txt' size='40'> "
    "<button onclick='loadFile()'>Load</button> <button onclick='saveFile()'>Save</button> "
    "<a id='dl' href='#' onclick='downloadFile();return false;'>Download</a></div>"
    "<textarea id='ta' placeholder='Text editor'></textarea>"
    "<hr>"
    "<h3>Upload</h3>"
    "<div>Dest path: <input id='upPath' placeholder='e.g. files/your.bin' size='40'></div>"
    "<input type='file' id='file'> <button onclick='uploadFile()'>Upload</button>"
    "<hr>"
    "<h3>List</h3>"
    "<div>Dir: <input id='dir' value='' placeholder='e.g. files' size='30'> <button onclick='listDir()'>List</button></div>"
    "<pre id='out'></pre>"
    "<script>"
    "function qs(id){return document.getElementById(id);}"
    "function root(){return qs('root').value;}"
    "function enc(s){return encodeURIComponent(s||'');}"
    "async function loadFile(){"
    " const p=qs('path').value.trim();"
    " const r=await fetch(`/api/read?root=${enc(root())}&path=${enc(p)}`);"
    " const t=await r.text();"
    " if(!r.ok){qs('out').textContent=t;return;}"
    " qs('ta').value=t; qs('out').textContent='Loaded '+p;"
    "}"
    "async function saveFile(){"
    " const p=qs('path').value.trim();"
    " const body=qs('ta').value;"
    " const r=await fetch(`/api/save?root=${enc(root())}&path=${enc(p)}`,{method:'POST',headers:{'Content-Type':'text/plain;charset=utf-8'},body});"
    " const t=await r.text(); qs('out').textContent=t;"
    "}"
    "function downloadFile(){"
    " const p=qs('path').value.trim();"
    " const url=`/api/download?root=${enc(root())}&path=${enc(p)}`;"
    " window.location.href=url;"
    "}"
    "async function uploadFile(){"
    " const f=qs('file').files[0];"
    " const p=qs('upPath').value.trim() || (f?f.name:'');"
    " if(!f){qs('out').textContent='Select a file.';return;}"
    " const r=await fetch(`/api/upload?root=${enc(root())}&path=${enc(p)}`,{method:'POST',body:f});"
    " qs('out').textContent=await r.text();"
    "}"
    "async function listDir(){"
    " const d=qs('dir').value.trim();"
    " const r=await fetch(`/api/list?root=${enc(root())}&dir=${enc(d)}`);"
    " qs('out').textContent=await r.text();"
    "}"
    "</script></body></html>";

static bool sanitize_rel_path(const char *in, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!in || in[0] == '\0') {
        return true; // empty allowed
    }

    // Normalize slashes and strip leading '/'.
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
        char c = in[i];
        if (c == '\\') c = '/';
        if (j == 0 && c == '/') {
            continue;
        }
        if (j + 1 >= out_len) {
            return false;
        }
        out[j++] = c;
    }
    out[j] = '\0';

    // Reject path traversal.
    if (strstr(out, "..") != nullptr) {
        return false;
    }
    // Reject drive-like or protocol-like paths.
    if (strchr(out, ':') != nullptr) {
        return false;
    }
    return true;
}

static const char *mount_for_root(const char *root)
{
    if (root && strcmp(root, "sd") == 0) {
        return "/sdcard";
    }
    return "/storage";
}

static esp_err_t ensure_parent_dirs(const char *full_path)
{
    if (!full_path || full_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char tmp[256];
    strncpy(tmp, full_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    // Walk and mkdir each component, skipping the file part.
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            (void)mkdir(tmp, 0775);
            *p = '/';
        }
    }
    return ESP_OK;
}

static esp_err_t send_text(httpd_req_t *req, int status, const char *text)
{
    if (!req) return ESP_ERR_INVALID_ARG;

    auto status_str = [](int s) -> const char * {
        switch (s) {
            case 200: return HTTPD_200;
            case 400: return HTTPD_400;
            case 404: return HTTPD_404;
            case 500: return HTTPD_500;
            case 401: return "401 Unauthorized";
            case 403: return "403 Forbidden";
            case 409: return "409 Conflict";
            default: return HTTPD_500;
        }
    };

    if (status != 200) {
        httpd_resp_set_status(req, status_str(status));
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, text ? text : "", HTTPD_RESP_USE_STRLEN);
}

static bool get_qs_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    if (!req || !key || !out || out_len == 0) return false;
    out[0] = '\0';
    const size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen <= 1) return false;
    char *q = (char *)calloc(1, qlen);
    if (!q) return false;
    bool ok = false;
    if (httpd_req_get_url_query_str(req, q, qlen) == ESP_OK) {
        if (httpd_query_key_value(q, key, out, out_len) == ESP_OK) {
            ok = true;
        }
    }
    free(q);
    return ok;
}

static esp_err_t handle_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_list(httpd_req_t *req)
{
    char root[8] = {0};
    char dir_in[192] = {0};
    (void)get_qs_value(req, "root", root, sizeof(root));
    (void)get_qs_value(req, "dir", dir_in, sizeof(dir_in));

    char dir_rel[192] = {0};
    if (!sanitize_rel_path(dir_in, dir_rel, sizeof(dir_rel))) {
        return send_text(req, 400, "Invalid dir");
    }

    const char *mount = mount_for_root(root);
    if (strcmp(mount, "/storage") == 0) {
        (void)storage_service_mount();
    } else {
        // Do not auto-mount SD here; listing will fail cleanly if unmounted.
        if (!sdcard_service_is_mounted()) {
            return send_text(req, 409, "SD not mounted");
        }
    }

    char full[256];
    if (dir_rel[0] == '\0') {
        snprintf(full, sizeof(full), "%s", mount);
    } else {
        snprintf(full, sizeof(full), "%s/%s", mount, dir_rel);
    }

    DIR *d = opendir(full);
    if (!d) {
        char msg[96];
        snprintf(msg, sizeof(msg), "opendir failed: %d", errno);
        return send_text(req, 404, msg);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");

    bool first = true;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        char item_full[256];
        snprintf(item_full, sizeof(item_full), "%s/%s", full, ent->d_name);
        struct stat st;
        memset(&st, 0, sizeof(st));
        (void)stat(item_full, &st);

        const bool is_dir = S_ISDIR(st.st_mode);
        char chunk[256];
        snprintf(chunk, sizeof(chunk),
                 "%s{\"name\":\"%s\",\"type\":\"%s\",\"size\":%" PRIdMAX "}",
                 first ? "" : ",",
                 ent->d_name,
                 is_dir ? "dir" : "file",
                 (intmax_t)st.st_size);
        first = false;
        httpd_resp_sendstr_chunk(req, chunk);
    }
    closedir(d);

    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, nullptr);
}

static esp_err_t handle_download(httpd_req_t *req)
{
    char root[8] = {0};
    char path_in[192] = {0};
    (void)get_qs_value(req, "root", root, sizeof(root));
    if (!get_qs_value(req, "path", path_in, sizeof(path_in))) {
        return send_text(req, 400, "Missing path");
    }

    char rel[192] = {0};
    if (!sanitize_rel_path(path_in, rel, sizeof(rel)) || rel[0] == '\0') {
        return send_text(req, 400, "Invalid path");
    }

    const char *mount = mount_for_root(root);
    if (strcmp(mount, "/storage") == 0) {
        (void)storage_service_mount();
    } else if (!sdcard_service_is_mounted()) {
        return send_text(req, 409, "SD not mounted");
    }

    char full[256];
    snprintf(full, sizeof(full), "%s/%s", mount, rel);

    FILE *f = fopen(full, "rb");
    if (!f) {
        return send_text(req, 404, "Not found");
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // Best-effort filename.
    const char *fn = strrchr(rel, '/');
    fn = fn ? (fn + 1) : rel;
    char disp[128];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", fn);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, nullptr, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t handle_read(httpd_req_t *req)
{
    char root[8] = {0};
    char path_in[192] = {0};
    (void)get_qs_value(req, "root", root, sizeof(root));
    if (!get_qs_value(req, "path", path_in, sizeof(path_in))) {
        return send_text(req, 400, "Missing path");
    }

    char rel[192] = {0};
    if (!sanitize_rel_path(path_in, rel, sizeof(rel)) || rel[0] == '\0') {
        return send_text(req, 400, "Invalid path");
    }

    const char *mount = mount_for_root(root);
    if (strcmp(mount, "/storage") == 0) {
        (void)storage_service_mount();
    } else if (!sdcard_service_is_mounted()) {
        return send_text(req, 409, "SD not mounted");
    }

    char full[256];
    snprintf(full, sizeof(full), "%s/%s", mount, rel);
    FILE *f = fopen(full, "rb");
    if (!f) {
        return send_text(req, 404, "Not found");
    }

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, nullptr, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t handle_save(httpd_req_t *req)
{
    char root[8] = {0};
    char path_in[192] = {0};
    (void)get_qs_value(req, "root", root, sizeof(root));
    if (!get_qs_value(req, "path", path_in, sizeof(path_in))) {
        return send_text(req, 400, "Missing path");
    }

    char rel[192] = {0};
    if (!sanitize_rel_path(path_in, rel, sizeof(rel)) || rel[0] == '\0') {
        return send_text(req, 400, "Invalid path");
    }

    const char *mount = mount_for_root(root);
    if (strcmp(mount, "/storage") == 0) {
        ESP_RETURN_ON_ERROR(storage_service_mount(), TAG, "storage mount failed");
    } else {
        if (!sdcard_service_is_mounted()) {
            return send_text(req, 409, "SD not mounted");
        }
    }

    char full[256];
    snprintf(full, sizeof(full), "%s/%s", mount, rel);
    (void)ensure_parent_dirs(full);

    FILE *f = fopen(full, "wb");
    if (!f) {
        return send_text(req, 500, "Open failed");
    }

    const int total = req->content_len;
    int received = 0;
    char buf[1024];
    while (received < total) {
        const int to_read = (total - received) > (int)sizeof(buf) ? (int)sizeof(buf) : (total - received);
        const int r = httpd_req_recv(req, buf, to_read);
        if (r <= 0) {
            fclose(f);
            return send_text(req, 500, "Receive failed");
        }
        fwrite(buf, 1, r, f);
        received += r;
    }
    fclose(f);
    return send_text(req, 200, "OK");
}

static esp_err_t handle_upload(httpd_req_t *req)
{
    // Upload uses raw body (application/octet-stream) sent by the web UI.
    char root[8] = {0};
    char path_in[192] = {0};
    (void)get_qs_value(req, "root", root, sizeof(root));
    if (!get_qs_value(req, "path", path_in, sizeof(path_in))) {
        return send_text(req, 400, "Missing path");
    }

    char rel[192] = {0};
    if (!sanitize_rel_path(path_in, rel, sizeof(rel)) || rel[0] == '\0') {
        return send_text(req, 400, "Invalid path");
    }

    const char *mount = mount_for_root(root);
    if (strcmp(mount, "/storage") == 0) {
        ESP_RETURN_ON_ERROR(storage_service_mount(), TAG, "storage mount failed");
    } else {
        if (!sdcard_service_is_mounted()) {
            return send_text(req, 409, "SD not mounted");
        }
    }

    char full[256];
    snprintf(full, sizeof(full), "%s/%s", mount, rel);
    (void)ensure_parent_dirs(full);

    FILE *f = fopen(full, "wb");
    if (!f) {
        return send_text(req, 500, "Open failed");
    }

    const int total = req->content_len;
    int received = 0;
    char buf[1024];
    while (received < total) {
        const int to_read = (total - received) > (int)sizeof(buf) ? (int)sizeof(buf) : (total - received);
        const int r = httpd_req_recv(req, buf, to_read);
        if (r <= 0) {
            fclose(f);
            return send_text(req, 500, "Receive failed");
        }
        fwrite(buf, 1, r, f);
        received += r;
    }

    fclose(f);
    return send_text(req, 200, "Uploaded");
}

static esp_err_t start_httpd(void)
{
    if (s_httpd) {
        return ESP_OK;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;

    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &cfg), TAG, "httpd_start failed");

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_index,
        .user_ctx = nullptr,
    };
    httpd_uri_t list_uri = {
        .uri = "/api/list",
        .method = HTTP_GET,
        .handler = handle_list,
        .user_ctx = nullptr,
    };
    httpd_uri_t dl_uri = {
        .uri = "/api/download",
        .method = HTTP_GET,
        .handler = handle_download,
        .user_ctx = nullptr,
    };
    httpd_uri_t read_uri = {
        .uri = "/api/read",
        .method = HTTP_GET,
        .handler = handle_read,
        .user_ctx = nullptr,
    };
    httpd_uri_t save_uri = {
        .uri = "/api/save",
        .method = HTTP_POST,
        .handler = handle_save,
        .user_ctx = nullptr,
    };
    httpd_uri_t up_uri = {
        .uri = "/api/upload",
        .method = HTTP_POST,
        .handler = handle_upload,
        .user_ctx = nullptr,
    };

    (void)httpd_register_uri_handler(s_httpd, &index_uri);
    (void)httpd_register_uri_handler(s_httpd, &list_uri);
    (void)httpd_register_uri_handler(s_httpd, &dl_uri);
    (void)httpd_register_uri_handler(s_httpd, &read_uri);
    (void)httpd_register_uri_handler(s_httpd, &save_uri);
    (void)httpd_register_uri_handler(s_httpd, &up_uri);

    return ESP_OK;
}

static void stop_httpd(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = nullptr;
    }
}

static esp_err_t wifi_start_ap(void)
{
    // Ensure netif/event loop are initialized (wifi_service_init does this already).
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    // Preserve previous STA config if present.
    memset(&s_prev_sta_cfg, 0, sizeof(s_prev_sta_cfg));
    s_prev_sta_cfg_valid = (esp_wifi_get_config(WIFI_IF_STA, &s_prev_sta_cfg) == ESP_OK);
    (void)esp_wifi_get_mode(&s_prev_mode);

    (void)esp_wifi_stop();

    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid, s_ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    ap_cfg.ap.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set ap mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), TAG, "set ap config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    ESP_LOGI(TAG, "SoftAP started: SSID=%s IP=%s", s_ap_ssid, kApIp);
    return ESP_OK;
}

static void wifi_restore_previous(void)
{
    if (s_prev_mode == WIFI_MODE_NULL) {
        return;
    }

    (void)esp_wifi_stop();
    (void)esp_wifi_set_mode(s_prev_mode);
    if (s_prev_sta_cfg_valid) {
        (void)esp_wifi_set_config(WIFI_IF_STA, &s_prev_sta_cfg);
    }
    (void)esp_wifi_start();
    // wifi_service's event handler will reconnect on STA_START.
}

esp_err_t fileserver_service_start(void)
{
    if (s_running) {
        return ESP_OK;
    }

    // Start SoftAP first; mounting flash storage can consume heap and may cause AP attach to fail.
    ESP_RETURN_ON_ERROR(wifi_start_ap(), TAG, "wifi_start_ap failed");

    // Best-effort flash storage mount (FileServer can still serve SD if flash mount fails).
    (void)storage_service_mount();
    ESP_RETURN_ON_ERROR(start_httpd(), TAG, "start_httpd failed");

    s_running = true;
    return ESP_OK;
}

void fileserver_service_stop(void)
{
    if (!s_running) {
        return;
    }

    stop_httpd();
    wifi_restore_previous();
    s_running = false;
}

bool fileserver_service_is_running(void)
{
    return s_running;
}

const char *fileserver_service_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *fileserver_service_ap_ip(void)
{
    return kApIp;
}
