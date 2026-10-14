// Copyright 2026 . All rights reserved.
// Author: PwaOS Team
// Email: daqingzd@pwaos.com
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// browser_app.c - Implementation of browser methods using PwaOS SDK
// Implements basic browser methods similar to Firefox: navigation, history, 
// JavaScript execution, window control, user agent, mouse/keyboard input, etc.

#include "runtime_client_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#if defined(_WIN32)
#include <windows.h>
#define PATH_SEPARATOR '\\'
#define F_OK 0
#else
#include <unistd.h>
#include <time.h>
#define PATH_SEPARATOR '/'
#endif

// Default window dimensions — chosen to look good on typical TV/display (1280×720 friendly)
// but capped to fit within screens as small as 800×600
#define DEFAULT_WINDOW_WIDTH  1024
#define DEFAULT_WINDOW_HEIGHT 640

const void* getStructInChain(const void* base, pwaosStructureType type) {
    const pwaosBaseInStructure* b = (const pwaosBaseInStructure*)base;
    while (b) {
        if (b->type == type) {
            return (const void*)b;
        }
        b = (const pwaosBaseInStructure*)b->next;
    }
    return NULL;
}

// Helper function to sleep for milliseconds
static void sleep_ms(int milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#endif
}

// =========================================================
// Settings ObjectInterface callbacks
// (must live outside main() and be static)
// =========================================================
static char g_settings_proxy_type[32] = "system";
static char g_settings_useragent[PWAOS_MAX_USER_AGENT_LEN] = {0};
static PWAOS_APP_HANDLE g_settings_app_handle = INVALID_PWAOS_HANDLE;

static bool settings_proxy_setter(const char* propJsonStr, void* userData) {
    (void)userData;
    if (!propJsonStr || strlen(propJsonStr) < 2) return false;
    size_t len = strlen(propJsonStr) - 2;
    if (len >= sizeof(g_settings_proxy_type)) len = sizeof(g_settings_proxy_type) - 1;
    strncpy(g_settings_proxy_type, propJsonStr + 1, len);
    g_settings_proxy_type[len] = '\0';
    return true;
}

static bool settings_proxy_getter(char* out, int maxLen, void* userData) {
    (void)userData;
    snprintf(out, maxLen, "\"%s\"", g_settings_proxy_type);
    return true;
}

static bool settings_useragent_setter(const char* propJsonStr, void* userData) {
    (void)userData;
    if (!propJsonStr || strlen(propJsonStr) < 2) return false;
    size_t len = strlen(propJsonStr) - 2;
    if (len >= PWAOS_MAX_USER_AGENT_LEN) len = PWAOS_MAX_USER_AGENT_LEN - 1;
    strncpy(g_settings_useragent, propJsonStr + 1, len);
    g_settings_useragent[len] = '\0';
    return true;
}

static bool settings_useragent_getter(char* out, int maxLen, void* userData) {
    (void)userData;
    snprintf(out, maxLen, "\"%s\"", g_settings_useragent);
    return true;
}

static bool settings_setProxy_cb(int argc, const char* argvJsonStr,
                                  char* out_result, int maxLen, void* userData) {
    (void)userData;
    if (argc < 1) { snprintf(out_result, maxLen, "\"error\""); return true; }
    char type_str[32] = {0};
    if (sscanf(argvJsonStr, "[\"%31[^\"]\"]", type_str) != 1) {
        snprintf(out_result, maxLen, "\"error\""); return true;
    }

    pwaosNetworkProxyType pt = PWAOS_NETWORK_PROXY_TYPE_SYSTEM;
    if      (strcmp(type_str, "direct") == 0) pt = PWAOS_NETWORK_PROXY_TYPE_DIRECT;
    else if (strcmp(type_str, "auto")   == 0) pt = PWAOS_NETWORK_PROXY_TYPE_AUTO_DETECT;
    else if (strcmp(type_str, "pac")    == 0) pt = PWAOS_NETWORK_PROXY_TYPE_PAC_SCRIPT;
    else if (strcmp(type_str, "fixed")  == 0) pt = PWAOS_NETWORK_PROXY_TYPE_FIXED_SERVERS;
    else if (strcmp(type_str, "system") == 0) pt = PWAOS_NETWORK_PROXY_TYPE_SYSTEM;

    pwaosNetworkProxyInfo info = {
        .type = PWAOS_TYPE_NETWORK_PROXY_INFO,
        .next = NULL,
        .proxyType = pt,
        .details = { .server = { .host = "", .port = 0, .username = "", .password = "" } },
    };

    // optional second arg: "host:port"
    if (argc >= 2) {
        const char* p = strchr(argvJsonStr, ',');
        if (p) {
            char host_port[128] = {0};
            if (sscanf(p + 1, "\"%127[^\"]\"]", host_port) == 1) {
                char* colon = strchr(host_port, ':');
                if (colon) {
                    *colon = '\0';
                    strncpy(info.details.server.host, host_port, MAX_URI_LEN - 1);
                    info.details.server.port = atoi(colon + 1);
                } else {
                    strncpy(info.details.server.host, host_port, MAX_URI_LEN - 1);
                }
            }
        }
    }

    pwaosResult result = PWAOS_RESULT_ERROR_INTERNAL;
    if (g_settings_app_handle != INVALID_PWAOS_HANDLE) {
        result = pwaosApp_setNetworkProxy(g_settings_app_handle, &info);
        if (result == PWAOS_RESULT_SUCCESS) {
            snprintf(out_result, maxLen, "\"ok\"");
            return true;
        }
    }

    return false;
}

static bool settings_getProxy_cb(int argc, const char* argvJsonStr,
                                  char* out_result, int maxLen, void* userData) {
    (void)userData; (void)argc; (void)argvJsonStr;
    pwaosNetworkProxyInfo info = {
        .type = PWAOS_TYPE_NETWORK_PROXY_INFO,
        .next = NULL,
    };
    if (g_settings_app_handle != INVALID_PWAOS_HANDLE)
        pwaosApp_getNetworkProxy(g_settings_app_handle, &info);
    switch (info.proxyType) {
        case PWAOS_NETWORK_PROXY_TYPE_DIRECT:
            snprintf(out_result, maxLen, "\"direct\"");
            break;
        case PWAOS_NETWORK_PROXY_TYPE_AUTO_DETECT:
            snprintf(out_result, maxLen, "\"auto\"");
            break;
        case PWAOS_NETWORK_PROXY_TYPE_PAC_SCRIPT:
            snprintf(out_result, maxLen,
                     "{\"type\":\"pac\",\"pac_url\":\"%s\",\"pac_mandatory\":%s}",
                     info.details.pac_script.pac_url,
                     info.details.pac_script.pac_mandatory ? "true" : "false");
            break;
        case PWAOS_NETWORK_PROXY_TYPE_FIXED_SERVERS:
            snprintf(out_result, maxLen,
                     "{\"type\":\"fixed\",\"host\":\"%s\",\"port\":%u,\"username\":\"%s\"}",
                     info.details.server.host,
                     (unsigned)info.details.server.port,
                     info.details.server.username);
            break;
        case PWAOS_NETWORK_PROXY_TYPE_SYSTEM:
            snprintf(out_result, maxLen, "\"system\"");
            break;
        default:
            snprintf(out_result, maxLen, "\"unknown\"");
            break;
    }
    return true;
}

static bool settings_setUserAgent_cb(int argc, const char* argvJsonStr,
                                      char* out_result, int maxLen, void* userData) {
    (void)userData;
    if (argc < 1) { snprintf(out_result, maxLen, "\"error\""); return true; }
    char ua[PWAOS_MAX_USER_AGENT_LEN] = {0};
    if (sscanf(argvJsonStr, "[\"%4095[^\"]\"]", ua) != 1) {
        snprintf(out_result, maxLen, "\"error\""); return true;
    }
    pwaosUserAgentInfo ua_info = {
        .type = PWAOS_TYPE_USER_AGENT_INFO,
        .next = NULL,
    };
    strncpy(ua_info.userAgent, ua, PWAOS_MAX_USER_AGENT_LEN - 1);
    if (g_settings_app_handle != INVALID_PWAOS_HANDLE)
        pwaosApp_setUserAgent(g_settings_app_handle, &ua_info);
    snprintf(out_result, maxLen, "\"ok\"");
    return true;
}

static bool settings_getUserAgent_cb(int argc, const char* argvJsonStr,
                                      char* out_result, int maxLen, void* userData) {
    (void)userData; (void)argc; (void)argvJsonStr;
    pwaosUserAgentInfo ua_info = {
        .type = PWAOS_TYPE_USER_AGENT_INFO,
        .next = NULL,
    };
    if (g_settings_app_handle != INVALID_PWAOS_HANDLE)
        pwaosApp_getUserAgent(g_settings_app_handle, &ua_info);
    snprintf(out_result, maxLen, "\"%s\"", ua_info.userAgent);
    return true;
}

// Global app state for callbacks
typedef struct {
    PWAOS_APP_HANDLE appHandle;
    int running;
    int page_loaded;
} BrowserState;

static BrowserState g_browser_state = {INVALID_PWAOS_HANDLE, 1, 0};

// Print error message
static void print_error(const char* function_name, pwaosResult result) {
    char err_str[PWAOS_MAX_RESULT_NAME_SIZE];
    pwaos_resultToString(result, err_str);
    printf("[ERROR] %s failed: %s (code: %d)\n", function_name, err_str, result);
}

// Print usage information
static void print_usage(const char* program_name) {
    printf("Usage: %s [OPTIONS] [URL]\n\n", program_name);
    printf("PwaOS Browser - A browser application using the PwaOS SDK\n");
    printf("Default: http://wwww.pwaos.net/webapps/browser/index.html\n\n");
    printf("Options:\n");
    printf("  -h, --help           Show this help message and exit\n");
    printf("  -v, --version        Show version information and exit\n");
    printf("  -w, --width  WIDTH   Set browser window width (default: %d, centered)\n", DEFAULT_WINDOW_WIDTH);
    printf("  -h, --height HEIGHT  Set browser window height (default: %d, centered)\n", DEFAULT_WINDOW_HEIGHT);
    printf("  -x, --xpos   X       Set browser window X position (default: centered on screen)\n");
    printf("  -y, --ypos   Y       Set browser window Y position (default: centered on screen)\n");
    printf("  -u, --user-agent UA  Set custom user agent string\n");
    printf("  -p, --proxy  TYPE    Set proxy type: system, direct, manual\n");
    printf("  -f, --fps    FPS     Set target frame rate (default: 60)\n");
    printf("  -d, --density DENS   Set display density/DPI (default: 1.0)\n");
    printf("  --remote-debugging-port PORT   Set devtools port number\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s\n", program_name);
    printf("  %s https://www.google.com\n", program_name);
    printf("  %s --width 1920 --height 1080 --fps 144 http://wwww.pwaos.net/webapps/browser/index.html\n", program_name);
    printf("  %s --user-agent \"Mozilla/5.0\" --proxy system\n", program_name);
    printf("\n");
    printf("Report bugs to: daqingzd@pwaos.com\n");
}

// Print version information
static void print_version() {
    printf("PwaOS Browser App v1.0.0\n");
    printf("Using PwaOS Runtime SDK v0.2.1\n");
}

// Parse command line arguments
typedef struct {
    const char* url;
    int width;
    int height;
    int x;
    int y;
    char user_agent[512];
    char proxy_type[32];
    int fps;
    float density;
    int show_help;
    int show_version;
} BrowserArgs;

static void init_args(BrowserArgs* args);  // forward — defined below
static int get_screen_size(int* out_w, int* out_h);  // forward — defined below

static void init_args(BrowserArgs* args) {
    args->url = "https://www.pwaos.net/webapps/browser/index.html";
    args->user_agent[0] = '\0';
    args->proxy_type[0] = '\0';
    args->fps = 60;
    args->density = 1.0f;
    args->show_help = 0;
    args->show_version = 0;

    // Apply default window size + centered position based on screen dimensions
    int screen_w = 0, screen_h = 0;
    get_screen_size(&screen_w, &screen_h);
    // Fallback to DEFAULT_WINDOW dimensions if screen detection failed
    if (screen_w <= 0) screen_w = DEFAULT_WINDOW_WIDTH;
    if (screen_h <= 0) screen_h = DEFAULT_WINDOW_HEIGHT;
    args->width = DEFAULT_WINDOW_WIDTH;
    args->height = DEFAULT_WINDOW_HEIGHT;
    args->x = (screen_w - args->width) / 2;
    args->y = (screen_h - args->height) / 2;
    if (args->x < 0) args->x = 0;
    if (args->y < 0) args->y = 0;
}

// Get screen dimensions for the primary display.
// Returns 0 on success, -1 on failure.
static int get_screen_size(int* out_w, int* out_h) {
#if defined(_WIN32)
    {
        HWND desktop = GetDesktopWindow();
        RECT rc;
        if (desktop && GetWindowRect(desktop, &rc)) {
            *out_w = rc.right - rc.left;
            *out_h = rc.bottom - rc.top;
            return 0;
        }
    }
#elif defined(__linux__)
    #if defined(HAVE_X11)
    {
        Display* dpy = XOpenDisplay(NULL);
        if (dpy) {
            Screen* scr = DefaultScreenOfDisplay(dpy);
            *out_w = WidthOfScreen(scr);
            *out_h = HeightOfScreen(scr);
            XCloseDisplay(dpy);
            return 0;
        }
    }
    #endif
    // Fallback: parse `xrandr` output for the active display size
    {
        FILE* fp = popen("xrandr 2>/dev/null | grep -m1 '\\*' | awk '{print $1}'", "r");
        if (fp) {
            char line[64] = {0};
            int ok = (fgets(line, sizeof(line), fp) != NULL);
            int pclose_rc = pclose(fp);
            if (ok && pclose_rc == 0) {
                int w = 0, h = 0;
                if (sscanf(line, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                    *out_w = w;
                    *out_h = h;
                    return 0;
                }
            }
        }
    }
#endif
    *out_w = 0;
    *out_h = 0;
    return -1;
}

static int parse_args(BrowserArgs* args, int argc, char** argv) {
    init_args(args);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            args->show_help = 1;
            return 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            args->show_version = 1;
            return 1;
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--width") == 0) {
            if (i + 1 < argc) {
                args->width = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--height") == 0) {
            if (i + 1 < argc) {
                args->height = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--xpos") == 0) {
            if (i + 1 < argc) {
                args->x = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--ypos") == 0) {
            if (i + 1 < argc) {
                args->y = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--user-agent") == 0) {
            if (i + 1 < argc) {
                strncpy(args->user_agent, argv[++i], sizeof(args->user_agent) - 1);
                args->user_agent[sizeof(args->user_agent) - 1] = '\0';
            }
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--proxy") == 0) {
            if (i + 1 < argc) {
                strncpy(args->proxy_type, argv[++i], sizeof(args->proxy_type) - 1);
                args->proxy_type[sizeof(args->proxy_type) - 1] = '\0';
            }
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fps") == 0) {
            if (i + 1 < argc) {
                args->fps = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--density") == 0) {
            if (i + 1 < argc) {
                args->density = atof(argv[++i]);
            }
        } else if (argv[i][0] != '-') {
            // Assume it's a URL
            args->url = argv[i];
        }
    }
    return 0;
}

/**
 * Browser implementation using PwaOS SDK
 */
int main(int argc, char** argv) {
    BrowserArgs args;

    // Parse command line arguments first
    if (parse_args(&args, argc, argv)) {
        // Early return was requested (--help or --version)
        if (args.show_help) {
            print_usage(argv[0]);
            return 0;
        }
        if (args.show_version) {
            print_version();
            return 0;
        }
    }

    printf("PwaOS Browser App starting...\n");
    printf("URL: %s | Window: %dx%d | FPS: %d\n",
           args.url, args.width, args.height, args.fps);

    // Set runtime executable path
#if defined(_WIN32)
    pwaos_setRuntimeExecutablePath(PWAOS_SDK_DIR"\\runtime");
#else
    pwaos_setRuntimeExecutablePath(PWAOS_SDK_DIR"/runtime");
#endif

    // Initialize
    pwaosInitializeInfo init_info = {
        .type = PWAOS_TYPE_INITIALIZE_INFO,
        .next = NULL,
        .argc = argc,
        .argv = argv,
        .use_dedicated_thread = false,
    };

    pwaosResult result = pwaos_init(&init_info);
    if (result != PWAOS_RESULT_SUCCESS) {
        print_error("pwaos_init", result);
        return -1;
    }
    printf("[OK] Runtime initialized\n");

    // Create instance
    pwaosInstanceCreateInfo create_info = {
        .type = PWAOS_TYPE_INSTANCE_CREATE_INFO,
        .next = NULL,
        .runtimeType = runtimeTypePwa,
        .serverUrl = "http://localhost",
        .dataPath = "",
    };

    // Set data path
    if (argc > 0 && access(argv[0], F_OK) == 0) {
        char exe_path[PWAOS_MAX_FILE_PATH_LEN];
#if defined(_WIN32)
        _fullpath(exe_path, argv[0], PWAOS_MAX_FILE_PATH_LEN);
#else
        realpath(argv[0], exe_path);
#endif
        const char* last_sep = strrchr(exe_path, PATH_SEPARATOR);
        if (last_sep) {
            int dir_len = last_sep - exe_path;
            create_info.dataPath[0] = '\0';
            strncat(create_info.dataPath, exe_path, dir_len);
            strcat(create_info.dataPath, "/data");
        }
    }

    PWAOS_INSTANCE_HANDLE instance_handle = INVALID_PWAOS_HANDLE;
    result = pwaos_createInstance(&create_info, &instance_handle);
    if (result != PWAOS_RESULT_SUCCESS) {
        print_error("pwaos_createInstance", result);
        pwaos_shutDown();
        return -1;
    }
    printf("[OK] Instance created\n");

    // Wait for instance to be ready
    int instance_ready = 0;
    while (!instance_ready && g_browser_state.running) {
        pwaosEventDataBuffer buffer;
        if (PWAOS_RESULT_SUCCESS == pwaosInstance_pollEvent(instance_handle, &buffer)) {
            if (buffer.type == PWAOS_TYPE_EVENT_DATA_INSTANCE_STATUS_CHANGED) {
                const pwaosEventDataInstanceStatusChanged* event =
                    (const pwaosEventDataInstanceStatusChanged*)&buffer;
                if (event->instance == instance_handle) {
                    if (event->status == instanceStatusWorking) {
                        instance_ready = 1;
                    } else if (event->status == instanceStatusStopping ||
                               event->status < 0) {
                        printf("[WARN] Instance status: %d\n", event->status);
                    }
                }
            }
        } else {
            sleep_ms(10);
        }
    }

    if (!instance_ready) {
        printf("[ERROR] Instance did not become ready\n");
        pwaos_destroyInstance(instance_handle);
        pwaos_shutDown();
        return -1;
    }
    printf("[OK] Instance is working\n");

    // Create browser app with appTypeBrowser
    pwaosAppCreateInfo app_info = {
        .type = PWAOS_TYPE_PWA_CREATE_INFO,
        .next = NULL,
        .appType = appTypeBrowser,  // Run as native browser app
        .appUrl = args.url,
        .width = args.width,
        .height = args.height
    };
    strcpy(app_info.appUrl, args.url);

    // Setup custom user agent (from args or default)
    pwaosUserAgentInfo ua_info = {
        .type = PWAOS_TYPE_USER_AGENT_INFO,
        .next = NULL,
    };
    if (args.user_agent[0] != '\0') {
        strncpy(ua_info.userAgent, args.user_agent, MAX_URI_LEN - 1);
        ua_info.userAgent[MAX_URI_LEN - 1] = '\0';
    } else {
        snprintf(ua_info.userAgent, MAX_URI_LEN,
                 "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                 "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    }
    app_info.next = &ua_info;

    // Setup initial windows bounds
    pwaosWindowBoundsInfo bounds_info = {
        .type = PWAOS_TYPE_WINDOW_BOUNDS_INFO,
        .next = NULL,
        .x = args.x,
        .y = args.y,
        .width = args.width,
        .height = args.height
    };
    ua_info.next = &bounds_info;

    // Setup proxy
    pwaosNetworkProxyInfo proxy_info = {
        .type = PWAOS_TYPE_NETWORK_PROXY_INFO,
        .next = NULL,
        .proxyType = PWAOS_NETWORK_PROXY_TYPE_SYSTEM,
    };

    // Set proxy type from args (default: SYSTEM)
    if (args.proxy_type[0] != '\0') {
        if (strcmp(args.proxy_type, "system") == 0) {
            proxy_info.proxyType = PWAOS_NETWORK_PROXY_TYPE_SYSTEM;
        } else if (strcmp(args.proxy_type, "direct") == 0) {
            proxy_info.proxyType = PWAOS_NETWORK_PROXY_TYPE_DIRECT;
        } else if (strcmp(args.proxy_type, "auto") == 0) {
            proxy_info.proxyType = PWAOS_NETWORK_PROXY_TYPE_AUTO_DETECT;
        }
    }
    bounds_info.next = &proxy_info;

    PWAOS_APP_HANDLE app_handle = INVALID_PWAOS_HANDLE;
    result = pwaosInstance_createPwa(instance_handle, &app_info, &app_handle);
    if (result != PWAOS_RESULT_SUCCESS) {
        print_error("pwaosInstance_createPwa", result);
        pwaos_destroyInstance(instance_handle);
        pwaos_shutDown();
        return -1;
    }
    g_browser_state.appHandle = app_handle;
    printf("[OK] Browser app created with URL: %s\n", app_info.appUrl);

    // =========================================================
    // Browser Methods Implementation
    // =========================================================

    // =========================================================
    // 0. Settings ObjectInterface (proxy + useragent)
    // =========================================================
    g_settings_app_handle = app_handle;

    printf("\n--- 0. Settings ObjectInterface ---\n");
    {
        PWAOS_OBJECT_INTERFACE_HANDLE settings_obj = INVALID_PWAOS_HANDLE;
        pwaosObjectInterfaceCreateInfo obj_info = {
            .type = PWAOS_TYPE_OBJECT_INTERFACE_CREATE_INFO,
            .next = NULL,
            .constructor = NULL,
            .constructorUserData = NULL,
            .destructor = NULL,
            .destructorUserData = NULL,
        };
        result = pwaosInstance_createObjectInterface(instance_handle, &obj_info, &settings_obj);
        if (result != PWAOS_RESULT_SUCCESS) {
            print_error("createObjectInterface", result);
        } else {
            printf("[OK] Settings ObjectInterface created\n");

            // properties
            pwaosObjectInterface_defineProperty(settings_obj, "proxy",
                settings_proxy_setter, settings_proxy_getter, NULL);
            printf("[OK] Settings.proxy defined\n");

            pwaosObjectInterface_defineProperty(settings_obj, "userAgent",
                settings_useragent_setter, settings_useragent_getter, NULL);
            printf("[OK] Settings.userAgent defined\n");

            // methods
            pwaosObjectInterface_defineMethod(settings_obj, "setProxy",
                settings_setProxy_cb, NULL);
            printf("[OK] Settings.setProxy defined\n");

            pwaosObjectInterface_defineMethod(settings_obj, "getProxy",
                settings_getProxy_cb, NULL);
            printf("[OK] Settings.getProxy defined\n");

            pwaosObjectInterface_defineMethod(settings_obj, "setUserAgent",
                settings_setUserAgent_cb, NULL);
            printf("[OK] Settings.setUserAgent defined\n");

            pwaosObjectInterface_defineMethod(settings_obj, "getUserAgent",
                settings_getUserAgent_cb, NULL);
            printf("[OK] Settings.getUserAgent defined\n");

            // expose to JS as window.Settings
            result = pwaosApp_addJavascriptInterface(app_handle, "Settings", settings_obj);
            if (result == PWAOS_RESULT_SUCCESS) {
                printf("[OK] window.Settings ready\n");
            } else {
                print_error("addJavascriptInterface", result);
            }
        }
    }

    // 1. Get/Set User Agent (direct SDK call, for comparison)
    printf("\n--- 1. User Agent Test ---\n");
    {
        pwaosUserAgentInfo ua_info = {
            .type = PWAOS_TYPE_USER_AGENT_INFO,
            .next = NULL,
        };

        // Set custom user agent (from args or default)
        if (args.user_agent[0] != '\0') {
            strncpy(ua_info.userAgent, args.user_agent, MAX_URI_LEN - 1);
            ua_info.userAgent[MAX_URI_LEN - 1] = '\0';
        } else {
            snprintf(ua_info.userAgent, MAX_URI_LEN, 
                     "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        }
        result = pwaosApp_setUserAgent(app_handle, &ua_info);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[OK] User agent set\n");
        } else {
            printf("[INFO] setUserAgent: %d\n", result);
        }

        // Get user agent
        memset(ua_info.userAgent, 0, sizeof(ua_info.userAgent));
        result = pwaosApp_getUserAgent(app_handle, &ua_info);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[INFO] Current user agent: %s\n", ua_info.userAgent);
        }
    }

    // 2. Window Bounds - Get/Set Position and Size
    printf("\n--- 2. Window Bounds Test ---\n");
    {
        pwaosWindowBoundsInfo bounds = {
            .type = PWAOS_TYPE_WINDOW_BOUNDS_INFO,
            .next = NULL,
            .x = args.x,
            .y = args.y,
            .width = args.width,
            .height = args.height
        };

        // Set window position and size
        result = pwaosApp_setBounds(app_handle, &bounds);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[OK] Window bounds set to: x=%d, y=%d, w=%d, h=%d\n",
                   bounds.x, bounds.y, bounds.width, bounds.height);
        } else {
            print_error("pwaosApp_setBounds", result);
        }

        // Get window bounds
        pwaosWindowBoundsInfo get_bounds = {
            .type = PWAOS_TYPE_WINDOW_BOUNDS_INFO,
            .next = NULL
        };
        result = pwaosApp_getBounds(app_handle, &get_bounds);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[INFO] Current bounds: x=%d, y=%d, w=%d, h=%d\n",
                   get_bounds.x, get_bounds.y, get_bounds.width, get_bounds.height);
        }
    }

    // 3. Mouse/Pointer Input Handling
    printf("\n--- 3. Mouse Input Test ---\n");
    {
        // Simulate a click at center of window (640, 400)
        result = pwaosApp_handlePointerInput(app_handle, true, 640.0f, 400.0f);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[OK] Pointer input sent at (640, 400)\n");
        } else {
            printf("[INFO] handlePointerInput: %d\n", result);
        }

        // Simulate a click
        result = pwaosApp_handleClick(app_handle, true, true);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[OK] Click event sent\n");
        } else {
            printf("[INFO] handleClick: %d\n", result);
        }
    }

    // 4. JavaScript Execution
    printf("\n--- 4. JavaScript Execution Test ---\n");
    {
        // Wait a bit for page to load
        sleep_ms(2000);

        // Execute JavaScript to get document title
        const char* js_get_title = "document.title";
        result = pwaosApp_executeJavaScript(app_handle, js_get_title);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[OK] JavaScript executed: %s\n", js_get_title);
        } else {
            print_error("pwaosApp_executeJavaScript", result);
        }

        // Execute JavaScript to show an alert (for testing)
        const char* js_alert = "window.scrollTo(0, 0);";
        result = pwaosApp_executeJavaScript(app_handle, js_alert);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[OK] JavaScript executed: %s\n", js_alert);
        }
    }

    // 5. FPS Control
    printf("\n--- 5. FPS Control Test ---\n");
    {
        pwaosAppFpsInfo fps_info = {
            .type = PWAOS_TYPE_APP_FPS_INFO,
            .next = NULL,
            .fps = (float)args.fps
        };
        result = pwaosApp_setFPS(app_handle, &fps_info);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[OK] FPS set to: %.1f\n", fps_info.fps);
        } else {
            printf("[INFO] setFPS: %d\n", result);
        }
    }

    // 6. Network Proxy Settings
    printf("\n--- 6. Network Proxy Test ---\n");
    {
        pwaosNetworkProxyInfo proxy_info = {
            .type = PWAOS_TYPE_NETWORK_PROXY_INFO,
            .next = NULL,
            .proxyType = PWAOS_NETWORK_PROXY_TYPE_SYSTEM,
        };

        // Set proxy type from args (default: SYSTEM)
        if (args.proxy_type[0] != '\0') {
            if (strcmp(args.proxy_type, "system") == 0) {
                proxy_info.proxyType = PWAOS_NETWORK_PROXY_TYPE_SYSTEM;
            } else if (strcmp(args.proxy_type, "direct") == 0) {
                proxy_info.proxyType = PWAOS_NETWORK_PROXY_TYPE_DIRECT;
            } else if (strcmp(args.proxy_type, "auto") == 0) {
                proxy_info.proxyType = PWAOS_NETWORK_PROXY_TYPE_AUTO_DETECT;
            }
        }

        // Set system proxy (use system settings)
        result = pwaosApp_setNetworkProxy(app_handle, &proxy_info);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[OK] Network proxy set to SYSTEM\n");
        } else {
            printf("[INFO] setNetworkProxy: %d\n", result);
        }

        // Get current proxy settings
        result = pwaosApp_getNetworkProxy(app_handle, &proxy_info);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[INFO] Current proxy type: %d\n", proxy_info.proxyType);
        }
    }

    // 7. Density Control (for responsive design testing)
    printf("\n--- 7. Display Density Test ---\n");
    {
        float density = args.density;
        result = pwaosApp_setDensity(app_handle, density);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[OK] Display density set to: %.2f\n", density);
        } else {
            printf("[INFO] setDensity: %d\n", result);
        }

        float out_density = 0.0f;
        result = pwaosApp_getDensity(app_handle, &out_density);
        if (result == PWAOS_RESULT_SUCCESS) {
            printf("[INFO] Current display density: %.2f\n", out_density);
        }
    }

    // 8. DevTools URI (for debugging)
    printf("\n--- 8. DevTools URI Test ---\n");
    {
        pwaosUriInfo uri_info = {
            .type = PWAOS_TYPE_DEVTOOLS_URI_INFO,
            .next = NULL,
        };
        result = pwaosApp_getDevToolsUri(app_handle, &uri_info);
        if (result == PWAOS_RESULT_SUCCESS && strlen(uri_info.url) > 0) {
            printf("[OK] DevTools URI: %s\n", uri_info.url);
        } else {
            printf("[INFO] DevTools not available or not yet ready\n");
        }
    }

    // =========================================================
    // Main event loop - wait for app to close
    // =========================================================
    printf("\n--- Main Event Loop ---\n");
    printf("Browser is running. Waiting for events...\n");
    printf("Press Ctrl+C to exit or close the browser window.\n");

    while (g_browser_state.running) {
        pwaosEventDataBuffer buffer;
        if (PWAOS_RESULT_SUCCESS == pwaosInstance_pollEvent(instance_handle, &buffer)) {
            if (buffer.type == PWAOS_TYPE_EVENT_DATA_INSTANCE_STATUS_CHANGED) {
                const pwaosEventDataInstanceStatusChanged* event =
                    (const pwaosEventDataInstanceStatusChanged*)&buffer;
                if (event->instance == instance_handle) {
                    if (event->status == instanceStatusStopping ||
                        event->status < 0) {
                        printf("[EXIT_INSTANCE] Instance status changed: %d\n", event->status);
                        g_browser_state.running = 0;
                        break;
                    }
                }
            } else if (buffer.type == PWAOS_TYPE_EVENT_DATA_APP_STATUS_CHANGED) {
                const pwaosEventDataAppStatusChanged* event =
                    (const pwaosEventDataAppStatusChanged*)&buffer;
                if (event->app == app_handle) {
                    printf("[APP_STATUS] status=%d\n", event->status);
                    if (event->status == appStatusStopping) {
                        printf("[EXIT_APP] App received appStatusStopping, exiting\n");
                        const pwaosAppStoppingInfoExt* info = (const pwaosAppStoppingInfoExt*)getStructInChain(event, PWAOS_TYPE_APP_STOPPING_INFO_EXT);
                        if (info && (info->stoppingReason == PWAOS_APP_STOPPING_REASON_NETWORK || info->stoppingReason == PWAOS_APP_STOPPING_REASON_CRASH))
                          continue;

                        g_browser_state.running = 0;
                        break;
                    } else if (event->status == appStatusReady) {
                        printf("[INFO] Page loaded and ready\n");
                        g_browser_state.page_loaded = 1;
                    }
                }
            }
        } else {
            sleep_ms(10);
        }
    }

    // Cleanup
    printf("\n--- Cleanup ---\n");
    pwaosInstance_destroyPwa(app_handle);
    printf("[OK] App destroyed\n");

    pwaos_destroyInstance(instance_handle);
    printf("[OK] Instance destroyed\n");

    pwaos_shutDown();
    printf("[OK] Runtime shutdown\n");

    printf("PwaOS Browser App exited\n");
    return 0;
}
