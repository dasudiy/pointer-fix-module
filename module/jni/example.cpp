#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <android/log.h>
#include <android/input.h>
#include <sys/sysmacros.h>

#include "zygisk.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "PointerFix", __VA_ARGS__)

// Axis and source constants (defined here to avoid APP_PLATFORM version issues)
static constexpr int32_t FIX_AXIS_X = 0;
static constexpr int32_t FIX_AXIS_Y = 1;
static constexpr int32_t FIX_AXIS_RELATIVE_X = 27;
static constexpr int32_t FIX_AXIS_RELATIVE_Y = 28;
static constexpr int32_t FIX_SOURCE_MOUSE_RELATIVE = 0x00020004;

static inline int32_t swap_axis(int32_t axis) {
    switch (axis) {
        case FIX_AXIS_X: return FIX_AXIS_Y;
        case FIX_AXIS_Y: return FIX_AXIS_X;
        case FIX_AXIS_RELATIVE_X: return FIX_AXIS_RELATIVE_Y;
        case FIX_AXIS_RELATIVE_Y: return FIX_AXIS_RELATIVE_X;
        default: return axis;
    }
}

static inline bool is_x_axis(int32_t axis) {
    return axis == FIX_AXIS_X || axis == FIX_AXIS_RELATIVE_X;
}

// ========================================
// JNI hooks — covers all Java/Kotlin apps
// ========================================

// nativeGetAxisValue is @FastNative → receives JNIEnv* and jclass
typedef jfloat (*fn_jni_getAxisValue)(JNIEnv*, jclass, jlong, jint, jint, jint);
static fn_jni_getAxisValue orig_jni_getAxisValue = nullptr;

// nativeGetSource is @CriticalNative → NO JNIEnv*/jclass
typedef jint (*fn_jni_getSource)(jlong);
static fn_jni_getSource orig_jni_getSource = nullptr;

static jfloat hook_jni_getAxisValue(JNIEnv* env, jclass clazz, jlong nativePtr,
                                     jint axis, jint pointerIndex, jint historyPos) {
    if (orig_jni_getSource) {
        jint source = orig_jni_getSource(nativePtr);
        if (source == FIX_SOURCE_MOUSE_RELATIVE) {
            jint swapped = swap_axis(axis);
            if (swapped != axis) {
                jfloat v = orig_jni_getAxisValue(env, clazz, nativePtr, swapped, pointerIndex, historyPos);
                return is_x_axis(axis) ? -v : v;
            }
        }
    }
    return orig_jni_getAxisValue(env, clazz, nativePtr, axis, pointerIndex, historyPos);
}

// Pass-through hook; we only hook this to capture the original function pointer
static jint hook_jni_getSource(jlong nativePtr) {
    return orig_jni_getSource ? orig_jni_getSource(nativePtr) : 0;
}

// ========================================
// NDK PLT hooks — covers pure-native apps
// ========================================

static float (*orig_ndk_getAxisValue)(const AInputEvent*, int32_t, size_t);
static float (*orig_ndk_getX)(const AInputEvent*, size_t);
static float (*orig_ndk_getY)(const AInputEvent*, size_t);

static float hook_ndk_getAxisValue(const AInputEvent* event, int32_t axis, size_t idx) {
    if (AInputEvent_getSource(event) == FIX_SOURCE_MOUSE_RELATIVE) {
        int32_t swapped = swap_axis(axis);
        if (swapped != axis) {
            float v = orig_ndk_getAxisValue(event, swapped, idx);
            return is_x_axis(axis) ? -v : v;
        }
    }
    return orig_ndk_getAxisValue(event, axis, idx);
}

static float hook_ndk_getX(const AInputEvent* event, size_t idx) {
    if (AInputEvent_getSource(event) == FIX_SOURCE_MOUSE_RELATIVE)
        return orig_ndk_getY ? -orig_ndk_getY(event, idx) : 0;
    return orig_ndk_getX ? orig_ndk_getX(event, idx) : 0;
}

static float hook_ndk_getY(const AInputEvent* event, size_t idx) {
    if (AInputEvent_getSource(event) == FIX_SOURCE_MOUSE_RELATIVE)
        return orig_ndk_getX ? orig_ndk_getX(event, idx) : 0;
    return orig_ndk_getY ? orig_ndk_getY(event, idx) : 0;
}

// ========================================
// Zygisk Module
// ========================================

class PointerFixModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        // 1) Hook Java MotionEvent native methods — this is the critical fix
        //    that covers all Java/Kotlin apps including Moonlight, Steam Link, etc.
        JNINativeMethod methods[] = {
            {"nativeGetAxisValue", "(JIII)F", (void*)hook_jni_getAxisValue},
            {"nativeGetSource",    "(J)I",    (void*)hook_jni_getSource},
        };
        api->hookJniNativeMethods(env, "android/view/MotionEvent", methods, 2);
        orig_jni_getAxisValue = (fn_jni_getAxisValue)methods[0].fnPtr;
        orig_jni_getSource    = (fn_jni_getSource)methods[1].fnPtr;

        LOGD("JNI hooks: getAxisValue=%p, getSource=%p",
             orig_jni_getAxisValue, orig_jni_getSource);

        // 2) PLT hooks for NDK input functions — covers pure-native apps
        setupPltHooks();
    }

private:
    Api *api;
    JNIEnv *env;

    void setupPltHooks() {
        FILE *fp = fopen("/proc/self/maps", "r");
        if (!fp) return;

        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            char perms[5];
            unsigned int dev_major, dev_minor;
            unsigned long inode;
            char path[256] = {0};

            if (sscanf(line, "%*s %4s %*s %x:%x %lu %255s",
                       perms, &dev_major, &dev_minor, &inode, path) >= 4
                && inode != 0 && perms[0] == 'r') {
                dev_t dev = makedev(dev_major, dev_minor);
                api->pltHookRegister(dev, inode, "AMotionEvent_getAxisValue",
                                     (void*)hook_ndk_getAxisValue, (void**)&orig_ndk_getAxisValue);
                api->pltHookRegister(dev, inode, "AMotionEvent_getX",
                                     (void*)hook_ndk_getX, (void**)&orig_ndk_getX);
                api->pltHookRegister(dev, inode, "AMotionEvent_getY",
                                     (void*)hook_ndk_getY, (void**)&orig_ndk_getY);
            }
        }
        fclose(fp);

        if (api->pltHookCommit()) {
            LOGD("PLT hooks committed");
        } else {
            LOGD("PLT hooks commit failed");
        }
    }
};

REGISTER_ZYGISK_MODULE(PointerFixModule)
