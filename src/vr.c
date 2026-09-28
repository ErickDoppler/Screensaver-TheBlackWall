/* OpenXR, without the loader library.
 *
 * The usual way to reach a headset is to link Khronos' loader and ship
 * openxr_loader.dll beside the program. This screensaver is one file with
 * nothing beside it, so instead it does what the loader does on Windows: read
 * the active runtime's path from the registry, load that DLL, and take
 * xrGetInstanceProcAddr from it. Every other entry point comes from there.
 *
 * What is given up is the loader's API layers (validation, the runtime's own
 * overlays). Nothing a screensaver needs.
 */
#include "vr.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef _WIN32
int  vr_present(void) { return 0; }
int  vr_init(void) { return 0; }
void vr_shutdown(void) {}
int  vr_running(void) { return 0; }
int  vr_begin_frame(void) { return 0; }
int  vr_should_render(void) { return 0; }
int  vr_view_count(void) { return 0; }
const VrView *vr_view(int i) { (void)i; return NULL; }
void vr_end_frame(void) {}
const VrInput *vr_input(void) { static VrInput z; return &z; }
void vr_recentre(void) {}
void vr_set_render_scale(float scale) { (void)scale; }
#else

/* the OpenXR structs are filled field by field; their `next` chains stay null */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>          /* some Windows OpenXR extensions name IUnknown */
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <GL/gl.h>
#include "../third_party/openxr/openxr.h"
#include "../third_party/openxr/openxr_platform.h"
#include "../third_party/openxr/openxr_loader_negotiation.h"

#define MAX_VIEWS 2

/* ---- the entry points we use -------------------------------------------- */
#define XR_FUNCS(X)                       \
    X(xrEnumerateInstanceExtensionProperties) \
    X(xrCreateInstance)                   \
    X(xrDestroyInstance)                  \
    X(xrGetInstanceProperties)            \
    X(xrGetSystem)                        \
    X(xrGetSystemProperties)              \
    X(xrEnumerateViewConfigurationViews)  \
    X(xrEnumerateEnvironmentBlendModes)   \
    X(xrCreateSession)                    \
    X(xrDestroySession)                   \
    X(xrBeginSession)                     \
    X(xrEndSession)                       \
    X(xrPollEvent)                        \
    X(xrCreateReferenceSpace)             \
    X(xrDestroySpace)                     \
    X(xrEnumerateSwapchainFormats)        \
    X(xrCreateSwapchain)                  \
    X(xrDestroySwapchain)                 \
    X(xrEnumerateSwapchainImages)         \
    X(xrAcquireSwapchainImage)            \
    X(xrWaitSwapchainImage)               \
    X(xrReleaseSwapchainImage)            \
    X(xrWaitFrame)                        \
    X(xrBeginFrame)                       \
    X(xrEndFrame)                         \
    X(xrLocateViews)                      \
    X(xrLocateSpace)                      \
    X(xrCreateActionSet)                  \
    X(xrDestroyActionSet)                 \
    X(xrCreateAction)                     \
    X(xrStringToPath)                     \
    X(xrSuggestInteractionProfileBindings) \
    X(xrAttachSessionActionSets)          \
    X(xrSyncActions)                      \
    X(xrGetActionStateFloat)              \
    X(xrGetActionStateBoolean)            \
    X(xrGetActionStateVector2f)           \
    X(xrCreateActionSpace)

#define X(n) static PFN_##n p##n;
XR_FUNCS(X)
#undef X
static PFN_xrGetInstanceProcAddr pxrGetInstanceProcAddr;
static PFN_xrGetOpenGLGraphicsRequirementsKHR pxrGetOpenGLGraphicsRequirementsKHR;

/* ---- state -------------------------------------------------------------- */
typedef struct Hand {
    XrSpace aim, grip;
} Hand;

static struct {
    HMODULE          dll;
    XrInstance       inst;
    XrSystemId       system;
    XrSession        session;
    XrSpace          space;              /* the play space */
    XrSessionState   state;
    XrSwapchain      chain[MAX_VIEWS];
    XrSwapchainImageOpenGLKHR *images[MAX_VIEWS];
    uint32_t         image_count[MAX_VIEWS];
    XrViewConfigurationView cfg[MAX_VIEWS];
    XrView           xviews[MAX_VIEWS];
    XrCompositionLayerProjectionView proj_views[MAX_VIEWS];
    VrView           views[MAX_VIEWS];
    uint32_t         view_count;
    XrFrameState     frame;
    XrEnvironmentBlendMode blend;
    int              running, should_render, frame_begun;
    uint32_t         acquired[MAX_VIEWS];
    /* input */
    XrActionSet      actions;
    XrAction         a_aim, a_grip, a_squeeze, a_trigger, a_stick, a_stick_click,
                     a_lower, a_upper;
    Hand             hand[VR_HANDS];
    XrPath           hand_path[VR_HANDS];
    VrInput          in;
    int              prev_lower[VR_HANDS], prev_upper[VR_HANDS], prev_click[VR_HANDS];
    float            render_scale;
} V;

static int xr_ok(XrResult r, const char *what) {
    if (XR_SUCCEEDED(r)) return 1;
    plat_log("vr: %s failed (%d)", what, (int)r);
    return 0;
}

/* ---- finding the runtime ------------------------------------------------- */
/* HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime points at a small JSON file
 * whose "library_path" is the runtime's DLL - relative to the JSON if it is
 * not absolute. That is the whole of what the loader does for us. */
static int read_active_runtime(char *out, int cap) {
    HKEY k;
    static const char *paths[] = { "SOFTWARE\\Khronos\\OpenXR\\1", "SOFTWARE\\Khronos\\OpenXR\\2" };
    for (int i = 0; i < 2; ++i) {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, paths[i], 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
            continue;
        DWORD type = 0, n = (DWORD)cap;
        LONG r = RegQueryValueExA(k, "ActiveRuntime", NULL, &type, (BYTE *)out, &n);
        RegCloseKey(k);
        if (r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) { out[cap - 1] = 0; return 1; }
    }
    return 0;
}

static int json_library_path(const char *json_path, char *out, int cap) {
    FILE *f = fopen(json_path, "rb");
    if (!f) return 0;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    const char *k = strstr(buf, "\"library_path\"");
    if (!k) return 0;
    k = strchr(k + 14, '"');
    if (!k) return 0;
    const char *s = k + 1, *e = strchr(s, '"');
    if (!e) return 0;
    char lib[1024];
    int len = (int)(e - s);
    if (len <= 0 || len >= (int)sizeof lib) return 0;
    /* the JSON escapes its backslashes */
    int o = 0;
    for (int i = 0; i < len && o < (int)sizeof lib - 1; ++i) {
        if (s[i] == '\\' && i + 1 < len && s[i + 1] == '\\') ++i;
        lib[o++] = s[i];
    }
    lib[o] = 0;
    int absolute = (lib[0] && lib[1] == ':') || lib[0] == '\\' || lib[0] == '/';
    if (absolute) { snprintf(out, (size_t)cap, "%s", lib); return 1; }
    /* relative to the folder the JSON sits in */
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", json_path);
    char *slash = strrchr(dir, '\\');
    char *slash2 = strrchr(dir, '/');
    if (slash2 > slash) slash = slash2;
    if (slash) *slash = 0; else dir[0] = 0;
    /* A path that does not fit is no path at all: say so rather than hand
     * back a truncated one for LoadLibrary to fail on. */
    if (snprintf(out, (size_t)cap, "%s\\%s", dir, lib) >= cap) {
        plat_log("vr: the runtime's path is too long: %s\\%s", dir, lib);
        return 0;
    }
    return 1;
}

static int load_runtime(void) {
    if (V.dll) return 1;
    char json[1024], dllpath[1024];
    if (!read_active_runtime(json, sizeof json)) { plat_log("vr: no OpenXR runtime registered"); return 0; }
    if (!json_library_path(json, dllpath, sizeof dllpath)) {
        plat_log("vr: cannot read the runtime manifest %s", json);
        return 0;
    }
    V.dll = LoadLibraryA(dllpath);
    if (!V.dll) { plat_log("vr: cannot load the runtime %s (%lu)", dllpath, GetLastError()); return 0; }
    /* A runtime does not simply export xrGetInstanceProcAddr: the loader is
     * expected to negotiate for it, agreeing an interface version first. That
     * handshake is the rest of what the loader would have done for us. */
    PFN_xrNegotiateLoaderRuntimeInterface negotiate =
        (PFN_xrNegotiateLoaderRuntimeInterface)(void *)GetProcAddress(V.dll, "xrNegotiateLoaderRuntimeInterface");
    if (negotiate) {
        XrNegotiateLoaderInfo li;
        memset(&li, 0, sizeof li);
        li.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
        li.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
        li.structSize = sizeof li;
        li.minInterfaceVersion = 1;
        li.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
        li.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
        li.maxApiVersion = XR_MAKE_VERSION(1, 1, 255);
        XrNegotiateRuntimeRequest rq;
        memset(&rq, 0, sizeof rq);
        rq.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
        rq.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
        rq.structSize = sizeof rq;
        XrResult nr = negotiate(&li, &rq);
        if (XR_SUCCEEDED(nr) && rq.getInstanceProcAddr) {
            pxrGetInstanceProcAddr = rq.getInstanceProcAddr;
            plat_log("vr: runtime interface %u, API %u.%u", (unsigned)rq.runtimeInterfaceVersion,
                     (unsigned)XR_VERSION_MAJOR(rq.runtimeApiVersion), (unsigned)XR_VERSION_MINOR(rq.runtimeApiVersion));
        } else {
            plat_log("vr: the runtime refused the handshake (%d)", (int)nr);
        }
    }
    if (!pxrGetInstanceProcAddr)   /* some runtimes export it anyway */
        pxrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)(void *)GetProcAddress(V.dll, "xrGetInstanceProcAddr");
    if (!pxrGetInstanceProcAddr) {
        plat_log("vr: the runtime gives no entry point");
        FreeLibrary(V.dll); V.dll = NULL; return 0;
    }
    plat_log("vr: runtime %s", dllpath);
    return 1;
}

static int load_funcs(XrInstance inst) {
#define X(n)                                                                   \
    if (XR_FAILED(pxrGetInstanceProcAddr(inst, #n, (PFN_xrVoidFunction *)&p##n)) || !p##n) { \
        plat_log("vr: the runtime is missing %s", #n); return 0; }
    XR_FUNCS(X)
#undef X
    pxrGetInstanceProcAddr(inst, "xrGetOpenGLGraphicsRequirementsKHR",
                           (PFN_xrVoidFunction *)&pxrGetOpenGLGraphicsRequirementsKHR);
    return 1;
}

/* ---- maths --------------------------------------------------------------- */
static basis3 basis_of_quat(XrQuaternionf q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    basis3 b;
    b.x = v3(1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w));
    b.y = v3(2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w));
    b.z = v3(2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y));
    return b;
}

/* ---- presence ------------------------------------------------------------ */
int vr_present(void) {
    char json[1024];
    if (!read_active_runtime(json, sizeof json)) return 0;
    char dll[1024];
    if (!json_library_path(json, dll, sizeof dll)) return 0;
    return GetFileAttributesA(dll) != INVALID_FILE_ATTRIBUTES;
}

/* ---- input --------------------------------------------------------------- */
static XrPath path_of(const char *s) {
    XrPath p = XR_NULL_PATH;
    pxrStringToPath(V.inst, s, &p);
    return p;
}

static XrAction make_action(XrActionType type, const char *name, const char *label) {
    XrActionCreateInfo ai = { XR_TYPE_ACTION_CREATE_INFO };
    ai.actionType = type;
    snprintf(ai.actionName, sizeof ai.actionName, "%s", name);
    snprintf(ai.localizedActionName, sizeof ai.localizedActionName, "%s", label);
    ai.countSubactionPaths = VR_HANDS;
    ai.subactionPaths = V.hand_path;
    XrAction a = XR_NULL_HANDLE;
    xr_ok(pxrCreateAction(V.actions, &ai, &a), "xrCreateAction");
    return a;
}

static void suggest(const char *profile, const XrActionSuggestedBinding *b, int n) {
    XrInteractionProfileSuggestedBinding s = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    s.interactionProfile = path_of(profile);
    s.countSuggestedBindings = (uint32_t)n;
    s.suggestedBindings = b;
    XrResult r = pxrSuggestInteractionProfileBindings(V.inst, &s);
    if (XR_FAILED(r)) plat_log("vr: %s bindings refused (%d)", profile, (int)r);
}

static int setup_input(void) {
    V.hand_path[0] = path_of("/user/hand/left");
    V.hand_path[1] = path_of("/user/hand/right");

    XrActionSetCreateInfo si = { XR_TYPE_ACTION_SET_CREATE_INFO };
    snprintf(si.actionSetName, sizeof si.actionSetName, "theblackwall");
    snprintf(si.localizedActionSetName, sizeof si.localizedActionSetName, "The Black Wall");
    if (!xr_ok(pxrCreateActionSet(V.inst, &si, &V.actions), "xrCreateActionSet")) return 0;

    V.a_aim         = make_action(XR_ACTION_TYPE_POSE_INPUT,    "aim",     "Pointer");
    V.a_grip        = make_action(XR_ACTION_TYPE_POSE_INPUT,    "grip",    "Hand");
    V.a_squeeze     = make_action(XR_ACTION_TYPE_FLOAT_INPUT,   "squeeze", "Grip");
    V.a_trigger     = make_action(XR_ACTION_TYPE_FLOAT_INPUT,   "trigger", "Trigger");
    V.a_stick       = make_action(XR_ACTION_TYPE_VECTOR2F_INPUT,"stick",   "Stick");
    V.a_stick_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "stickclick", "Stick click");
    V.a_lower       = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "lower",   "A / X");
    V.a_upper       = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "upper",   "B / Y");

    /* Oculus Touch: the four face buttons are A/B on the right, X/Y on the
     * left, so "lower" and "upper" mean the same finger on either hand. */
    {
        XrActionSuggestedBinding b[] = {
            { V.a_aim,     path_of("/user/hand/left/input/aim/pose") },
            { V.a_aim,     path_of("/user/hand/right/input/aim/pose") },
            { V.a_grip,    path_of("/user/hand/left/input/grip/pose") },
            { V.a_grip,    path_of("/user/hand/right/input/grip/pose") },
            { V.a_squeeze, path_of("/user/hand/left/input/squeeze/value") },
            { V.a_squeeze, path_of("/user/hand/right/input/squeeze/value") },
            { V.a_trigger, path_of("/user/hand/left/input/trigger/value") },
            { V.a_trigger, path_of("/user/hand/right/input/trigger/value") },
            { V.a_stick,   path_of("/user/hand/left/input/thumbstick") },
            { V.a_stick,   path_of("/user/hand/right/input/thumbstick") },
            { V.a_stick_click, path_of("/user/hand/left/input/thumbstick/click") },
            { V.a_stick_click, path_of("/user/hand/right/input/thumbstick/click") },
            { V.a_lower,   path_of("/user/hand/left/input/x/click") },
            { V.a_lower,   path_of("/user/hand/right/input/a/click") },
            { V.a_upper,   path_of("/user/hand/left/input/y/click") },
            { V.a_upper,   path_of("/user/hand/right/input/b/click") },
        };
        suggest("/interaction_profiles/oculus/touch_controller", b, (int)(sizeof b / sizeof *b));
    }
    /* Valve Index: A and B either side, and the grip is a force */
    {
        XrActionSuggestedBinding b[] = {
            { V.a_aim,     path_of("/user/hand/left/input/aim/pose") },
            { V.a_aim,     path_of("/user/hand/right/input/aim/pose") },
            { V.a_grip,    path_of("/user/hand/left/input/grip/pose") },
            { V.a_grip,    path_of("/user/hand/right/input/grip/pose") },
            { V.a_squeeze, path_of("/user/hand/left/input/squeeze/value") },
            { V.a_squeeze, path_of("/user/hand/right/input/squeeze/value") },
            { V.a_trigger, path_of("/user/hand/left/input/trigger/value") },
            { V.a_trigger, path_of("/user/hand/right/input/trigger/value") },
            { V.a_stick,   path_of("/user/hand/left/input/thumbstick") },
            { V.a_stick,   path_of("/user/hand/right/input/thumbstick") },
            { V.a_stick_click, path_of("/user/hand/left/input/thumbstick/click") },
            { V.a_stick_click, path_of("/user/hand/right/input/thumbstick/click") },
            { V.a_lower,   path_of("/user/hand/left/input/a/click") },
            { V.a_lower,   path_of("/user/hand/right/input/a/click") },
            { V.a_upper,   path_of("/user/hand/left/input/b/click") },
            { V.a_upper,   path_of("/user/hand/right/input/b/click") },
        };
        suggest("/interaction_profiles/valve/index_controller", b, (int)(sizeof b / sizeof *b));
    }
    /* HTC Vive wands: no sticks or face buttons - the trackpad and menu stand in */
    {
        XrActionSuggestedBinding b[] = {
            { V.a_aim,     path_of("/user/hand/left/input/aim/pose") },
            { V.a_aim,     path_of("/user/hand/right/input/aim/pose") },
            { V.a_grip,    path_of("/user/hand/left/input/grip/pose") },
            { V.a_grip,    path_of("/user/hand/right/input/grip/pose") },
            { V.a_squeeze, path_of("/user/hand/left/input/squeeze/click") },
            { V.a_squeeze, path_of("/user/hand/right/input/squeeze/click") },
            { V.a_trigger, path_of("/user/hand/left/input/trigger/value") },
            { V.a_trigger, path_of("/user/hand/right/input/trigger/value") },
            { V.a_stick,   path_of("/user/hand/left/input/trackpad") },
            { V.a_stick,   path_of("/user/hand/right/input/trackpad") },
            { V.a_stick_click, path_of("/user/hand/left/input/trackpad/click") },
            { V.a_stick_click, path_of("/user/hand/right/input/trackpad/click") },
            { V.a_upper,   path_of("/user/hand/left/input/menu/click") },
            { V.a_upper,   path_of("/user/hand/right/input/menu/click") },
        };
        suggest("/interaction_profiles/htc/vive_controller", b, (int)(sizeof b / sizeof *b));
    }
    /* anything else the runtime knows how to map */
    {
        XrActionSuggestedBinding b[] = {
            { V.a_aim,     path_of("/user/hand/left/input/aim/pose") },
            { V.a_aim,     path_of("/user/hand/right/input/aim/pose") },
            { V.a_grip,    path_of("/user/hand/left/input/grip/pose") },
            { V.a_grip,    path_of("/user/hand/right/input/grip/pose") },
            { V.a_trigger, path_of("/user/hand/left/input/select/click") },
            { V.a_trigger, path_of("/user/hand/right/input/select/click") },
            { V.a_upper,   path_of("/user/hand/left/input/menu/click") },
            { V.a_upper,   path_of("/user/hand/right/input/menu/click") },
        };
        suggest("/interaction_profiles/khr/simple_controller", b, (int)(sizeof b / sizeof *b));
    }

    for (int h = 0; h < VR_HANDS; ++h) {
        XrActionSpaceCreateInfo si2 = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        si2.poseInActionSpace.orientation.w = 1.f;
        si2.subactionPath = V.hand_path[h];
        si2.action = V.a_aim;
        pxrCreateActionSpace(V.session, &si2, &V.hand[h].aim);
        si2.action = V.a_grip;
        pxrCreateActionSpace(V.session, &si2, &V.hand[h].grip);
    }
    XrSessionActionSetsAttachInfo at = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    at.countActionSets = 1;
    at.actionSets = &V.actions;
    return xr_ok(pxrAttachSessionActionSets(V.session, &at), "xrAttachSessionActionSets");
}

/* ---- start-up ------------------------------------------------------------ */
int vr_init(void) {
    memset(&V, 0, sizeof V);
    V.render_scale = 1.f;
    if (!load_runtime()) return 0;

    const char *exts[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "The Black Wall");
    ci.applicationInfo.applicationVersion = 1;
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "The Black Wall");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = exts;
    /* the instance is the one call that comes straight off the runtime */
    PFN_xrCreateInstance create = NULL;
    if (XR_FAILED(pxrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&create)) || !create) {
        plat_log("vr: the runtime will not give xrCreateInstance");
        return 0;
    }
    XrResult r = create(&ci, &V.inst);
    if (XR_FAILED(r)) { plat_log("vr: xrCreateInstance failed (%d)", (int)r); return 0; }
    if (!load_funcs(V.inst)) { vr_shutdown(); return 0; }

    XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };
    if (XR_SUCCEEDED(pxrGetInstanceProperties(V.inst, &ip)))
        plat_log("vr: %s %u.%u.%u", ip.runtimeName,
                 (unsigned)XR_VERSION_MAJOR(ip.runtimeVersion), (unsigned)XR_VERSION_MINOR(ip.runtimeVersion),
                 (unsigned)XR_VERSION_PATCH(ip.runtimeVersion));

    XrSystemGetInfo gi = { XR_TYPE_SYSTEM_GET_INFO };
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = pxrGetSystem(V.inst, &gi, &V.system);
    if (XR_FAILED(r)) { plat_log("vr: no headset (%d)", (int)r); vr_shutdown(); return 0; }

    XrSystemProperties sp = { XR_TYPE_SYSTEM_PROPERTIES };
    if (XR_SUCCEEDED(pxrGetSystemProperties(V.inst, V.system, &sp)))
        plat_log("vr: headset %s", sp.systemName);

    /* the runtime insists on being asked what GL it needs before the session */
    if (pxrGetOpenGLGraphicsRequirementsKHR) {
        XrGraphicsRequirementsOpenGLKHR gr = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
        pxrGetOpenGLGraphicsRequirementsKHR(V.inst, V.system, &gr);
    }

    uint32_t n = 0;
    pxrEnumerateViewConfigurationViews(V.inst, V.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, NULL);
    if (n > MAX_VIEWS) n = MAX_VIEWS;
    for (uint32_t i = 0; i < n; ++i) V.cfg[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    pxrEnumerateViewConfigurationViews(V.inst, V.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n, V.cfg);
    V.view_count = n;
    if (!n) { plat_log("vr: no stereo views"); vr_shutdown(); return 0; }

    XrGraphicsBindingOpenGLWin32KHR gb = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    gb.hDC = wglGetCurrentDC();
    gb.hGLRC = wglGetCurrentContext();
    if (!gb.hDC || !gb.hGLRC) { plat_log("vr: no current GL context"); vr_shutdown(); return 0; }
    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &gb;
    sci.systemId = V.system;
    r = pxrCreateSession(V.inst, &sci, &V.session);
    if (XR_FAILED(r)) { plat_log("vr: xrCreateSession failed (%d)", (int)r); vr_shutdown(); return 0; }

    /* STAGE is explicitly floor-anchored (Y = 0 at the floor), which is what
     * the walking simulation needs for a real standing eye height. LOCAL's
     * own Y origin is left to the runtime, and some place it at the eye
     * instead of the floor - which the app would then read as height zero,
     * pinning the camera to the ground. Fall back to LOCAL only if the
     * runtime has no stage (a seated-only setup). */
    XrReferenceSpaceCreateInfo rs = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    rs.poseInReferenceSpace.orientation.w = 1.f;
    if (XR_FAILED(pxrCreateReferenceSpace(V.session, &rs, &V.space))) {
        plat_log("vr: no stage space, falling back to LOCAL (eye height may be off)");
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        if (!xr_ok(pxrCreateReferenceSpace(V.session, &rs, &V.space), "xrCreateReferenceSpace")) { vr_shutdown(); return 0; }
    }

    /* the colour format: the runtime's list, in its own order of preference */
    uint32_t fn = 0;
    pxrEnumerateSwapchainFormats(V.session, 0, &fn, NULL);
    int64_t *formats = calloc(fn ? fn : 1, sizeof *formats);
    pxrEnumerateSwapchainFormats(V.session, fn, &fn, formats);
    int64_t want = 0;
    for (uint32_t i = 0; i < fn && !want; ++i)
        if (formats[i] == 0x8C43 /* GL_SRGB8_ALPHA8 */) want = formats[i];
    for (uint32_t i = 0; i < fn && !want; ++i)
        if (formats[i] == 0x8058 /* GL_RGBA8 */) want = formats[i];
    if (!want && fn) want = formats[0];
    free(formats);

    for (uint32_t i = 0; i < V.view_count; ++i) {
        XrSwapchainCreateInfo sc = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sc.format = want;
        sc.sampleCount = 1;
        sc.width = V.cfg[i].recommendedImageRectWidth;
        sc.height = V.cfg[i].recommendedImageRectHeight;
        sc.faceCount = 1;
        sc.arraySize = 1;
        sc.mipCount = 1;
        if (!xr_ok(pxrCreateSwapchain(V.session, &sc, &V.chain[i]), "xrCreateSwapchain")) { vr_shutdown(); return 0; }
        uint32_t ic = 0;
        pxrEnumerateSwapchainImages(V.chain[i], 0, &ic, NULL);
        V.images[i] = calloc(ic, sizeof *V.images[i]);
        for (uint32_t j = 0; j < ic; ++j) V.images[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        pxrEnumerateSwapchainImages(V.chain[i], ic, &ic, (XrSwapchainImageBaseHeader *)V.images[i]);
        V.image_count[i] = ic;
        V.views[i].w = (int)sc.width;
        V.views[i].h = (int)sc.height;
        V.views[i].render_w = V.views[i].w;
        V.views[i].render_h = V.views[i].h;
    }
    plat_log("vr: %u views, %dx%d each", V.view_count, V.views[0].w, V.views[0].h);

    uint32_t bn = 0;
    V.blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    pxrEnumerateEnvironmentBlendModes(V.inst, V.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &bn, NULL);
    if (bn) {
        XrEnvironmentBlendMode *modes = calloc(bn, sizeof *modes);
        pxrEnumerateEnvironmentBlendModes(V.inst, V.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, bn, &bn, modes);
        V.blend = modes[0];
        free(modes);
    }
    if (!setup_input()) plat_log("vr: no controller input");
    for (uint32_t i = 0; i < MAX_VIEWS; ++i) V.xviews[i].type = XR_TYPE_VIEW;
    return 1;
}

void vr_shutdown(void) {
    for (uint32_t i = 0; i < V.view_count; ++i) {
        if (V.chain[i]) pxrDestroySwapchain(V.chain[i]);
        free(V.images[i]);
    }
    for (int h = 0; h < VR_HANDS; ++h) {
        if (V.hand[h].aim) pxrDestroySpace(V.hand[h].aim);
        if (V.hand[h].grip) pxrDestroySpace(V.hand[h].grip);
    }
    if (V.actions) pxrDestroyActionSet(V.actions);
    if (V.space) pxrDestroySpace(V.space);
    if (V.session) pxrDestroySession(V.session);
    if (V.inst) pxrDestroyInstance(V.inst);
    if (V.dll) FreeLibrary(V.dll);
    memset(&V, 0, sizeof V);
}

int vr_running(void) { return V.session != XR_NULL_HANDLE; }
int vr_view_count(void) { return (int)V.view_count; }
const VrView *vr_view(int i) { return (i >= 0 && (uint32_t)i < V.view_count) ? &V.views[i] : NULL; }
int vr_should_render(void) { return V.should_render; }
void vr_recentre(void) { /* the play space is the runtime's business */ }
void vr_set_render_scale(float scale) { V.render_scale = clampf(scale, 0.3f, 1.f); }

/* ---- the frame ----------------------------------------------------------- */
static void poll_events(void) {
    for (;;) {
        XrEventDataBuffer e = { XR_TYPE_EVENT_DATA_BUFFER };
        if (pxrPollEvent(V.inst, &e) != XR_SUCCESS) break;
        if (e.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *s = (const XrEventDataSessionStateChanged *)&e;
            V.state = s->state;
            if (s->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (xr_ok(pxrBeginSession(V.session, &bi), "xrBeginSession")) V.running = 1;
            } else if (s->state == XR_SESSION_STATE_STOPPING) {
                pxrEndSession(V.session);
                V.running = 0;
            }
            plat_log("vr: session state %d", (int)s->state);
        } else if (e.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            V.running = 0;
        }
    }
}

static void read_input(void) {
    if (!V.actions) return;
    XrActiveActionSet as = { V.actions, XR_NULL_PATH };
    XrActionsSyncInfo si = { XR_TYPE_ACTIONS_SYNC_INFO };
    si.countActiveActionSets = 1;
    si.activeActionSets = &as;
    if (XR_FAILED(pxrSyncActions(V.session, &si))) return;

    for (int h = 0; h < VR_HANDS; ++h) {
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        gi.subactionPath = V.hand_path[h];

        XrActionStateFloat f = { XR_TYPE_ACTION_STATE_FLOAT };
        gi.action = V.a_squeeze;
        V.in.squeeze[h] = (XR_SUCCEEDED(pxrGetActionStateFloat(V.session, &gi, &f)) && f.isActive) ? f.currentState : 0.f;
        gi.action = V.a_trigger;
        f.type = XR_TYPE_ACTION_STATE_FLOAT;
        V.in.trigger[h] = (XR_SUCCEEDED(pxrGetActionStateFloat(V.session, &gi, &f)) && f.isActive) ? f.currentState : 0.f;

        XrActionStateVector2f v = { XR_TYPE_ACTION_STATE_VECTOR2F };
        gi.action = V.a_stick;
        if (XR_SUCCEEDED(pxrGetActionStateVector2f(V.session, &gi, &v)) && v.isActive) {
            V.in.stick_x[h] = v.currentState.x;
            V.in.stick_y[h] = v.currentState.y;
        } else {
            V.in.stick_x[h] = V.in.stick_y[h] = 0.f;
        }

        XrActionStateBoolean b = { XR_TYPE_ACTION_STATE_BOOLEAN };
        gi.action = V.a_stick_click;
        V.in.stick_click[h] = (XR_SUCCEEDED(pxrGetActionStateBoolean(V.session, &gi, &b)) && b.isActive) ? b.currentState : 0;
        b.type = XR_TYPE_ACTION_STATE_BOOLEAN;
        gi.action = V.a_lower;
        V.in.lower[h] = (XR_SUCCEEDED(pxrGetActionStateBoolean(V.session, &gi, &b)) && b.isActive) ? b.currentState : 0;
        b.type = XR_TYPE_ACTION_STATE_BOOLEAN;
        gi.action = V.a_upper;
        V.in.upper[h] = (XR_SUCCEEDED(pxrGetActionStateBoolean(V.session, &gi, &b)) && b.isActive) ? b.currentState : 0;

        V.in.lower_edge[h] = V.in.lower[h] && !V.prev_lower[h];
        V.in.upper_edge[h] = V.in.upper[h] && !V.prev_upper[h];
        V.in.stick_click_edge[h] = V.in.stick_click[h] && !V.prev_click[h];
        V.prev_lower[h] = V.in.lower[h];
        V.prev_upper[h] = V.in.upper[h];
        V.prev_click[h] = V.in.stick_click[h];

        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        if (V.hand[h].aim && XR_SUCCEEDED(pxrLocateSpace(V.hand[h].aim, V.space, V.frame.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            V.in.aim_basis[h] = basis_of_quat(loc.pose.orientation);
            V.in.aim_pos[h] = v3(loc.pose.position.x, loc.pose.position.y, loc.pose.position.z);
            V.in.active[h] = 1;
        } else {
            V.in.active[h] = 0;
        }
        loc.type = XR_TYPE_SPACE_LOCATION;
        if (V.hand[h].grip && XR_SUCCEEDED(pxrLocateSpace(V.hand[h].grip, V.space, V.frame.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            V.in.grip_basis[h] = basis_of_quat(loc.pose.orientation);
            V.in.grip_pos[h] = v3(loc.pose.position.x, loc.pose.position.y, loc.pose.position.z);
        }
    }
}

int vr_begin_frame(void) {
    if (!V.session) return 0;
    poll_events();
    V.should_render = 0;
    if (!V.running) return 0;

    XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
    V.frame.type = XR_TYPE_FRAME_STATE;
    if (XR_FAILED(pxrWaitFrame(V.session, &wi, &V.frame))) return 0;
    XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
    if (XR_FAILED(pxrBeginFrame(V.session, &bi))) return 0;
    V.frame_begun = 1;
    read_input();
    if (!V.frame.shouldRender) return 1;        /* keep simulating, draw nothing */

    XrViewLocateInfo li = { XR_TYPE_VIEW_LOCATE_INFO };
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = V.frame.predictedDisplayTime;
    li.space = V.space;
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    uint32_t n = 0;
    if (XR_FAILED(pxrLocateViews(V.session, &li, &vs, V.view_count, &n, V.xviews))) return 1;
    if (!(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return 1;

    for (uint32_t i = 0; i < n && i < V.view_count; ++i) {
        XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        uint32_t idx = 0;
        if (XR_FAILED(pxrAcquireSwapchainImage(V.chain[i], &ai, &idx))) return 1;
        XrSwapchainImageWaitInfo wi2 = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wi2.timeout = XR_INFINITE_DURATION;
        if (XR_FAILED(pxrWaitSwapchainImage(V.chain[i], &wi2))) return 1;
        V.acquired[i] = idx;
        V.views[i].tex = V.images[i][idx].image;
        V.views[i].basis = basis_of_quat(V.xviews[i].pose.orientation);
        V.views[i].pos = v3(V.xviews[i].pose.position.x, V.xviews[i].pose.position.y, V.xviews[i].pose.position.z);
        V.views[i].tan_l = tanf(V.xviews[i].fov.angleLeft);
        V.views[i].tan_r = tanf(V.xviews[i].fov.angleRight);
        V.views[i].tan_d = tanf(V.xviews[i].fov.angleDown);
        V.views[i].tan_u = tanf(V.xviews[i].fov.angleUp);
        int rw = (int)(V.views[i].w * V.render_scale + 0.5f);
        int rh = (int)(V.views[i].h * V.render_scale + 0.5f);
        V.views[i].render_w = rw < 1 ? 1 : rw;
        V.views[i].render_h = rh < 1 ? 1 : rh;

        V.proj_views[i] = (XrCompositionLayerProjectionView){ XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
        V.proj_views[i].pose = V.xviews[i].pose;
        V.proj_views[i].fov = V.xviews[i].fov;
        V.proj_views[i].subImage.swapchain = V.chain[i];
        V.proj_views[i].subImage.imageRect.extent.width = V.views[i].render_w;
        V.proj_views[i].subImage.imageRect.extent.height = V.views[i].render_h;
        /* imageRect is top-left-origin (Vulkan/D3D convention) regardless of
         * graphics API, but glViewport(0, 0, render_w, render_h) - what
         * app.c actually renders with - fills the BOTTOM of the image in
         * OpenGL's own bottom-left convention. Below full resolution those
         * are different rows, so the offset has to shift down by the
         * shrunk margin or the compositor samples an unrendered strip. */
        V.proj_views[i].subImage.imageRect.offset.y = V.views[i].h - V.views[i].render_h;
    }
    V.should_render = 1;
    return 1;
}

void vr_end_frame(void) {
    if (!V.session || !V.frame_begun) return;
    if (V.should_render) {
        for (uint32_t i = 0; i < V.view_count; ++i) {
            XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            pxrReleaseSwapchainImage(V.chain[i], &ri);
        }
    }
    XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    layer.space = V.space;
    layer.viewCount = V.view_count;
    layer.views = V.proj_views;
    const XrCompositionLayerBaseHeader *layers[1] = { (const XrCompositionLayerBaseHeader *)&layer };
    XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
    ei.displayTime = V.frame.predictedDisplayTime;
    ei.environmentBlendMode = V.blend;
    ei.layerCount = V.should_render ? 1 : 0;
    ei.layers = V.should_render ? layers : NULL;
    pxrEndFrame(V.session, &ei);
    V.frame_begun = 0;
}

const VrInput *vr_input(void) { return &V.in; }
#endif /* _WIN32 */
