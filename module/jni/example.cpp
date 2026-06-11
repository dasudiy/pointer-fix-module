#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <android/input.h>
#include <sys/sysmacros.h>
#include <stdio.h>
#include <string.h>

#include "zygisk.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "PointerFix", __VA_ARGS__)

static float (*orig_AMotionEvent_getAxisValue)(const AInputEvent* motion_event, int32_t axis, size_t pointer_index);
static float (*orig_AMotionEvent_getX)(const AInputEvent* motion_event, size_t pointer_index);
static float (*orig_AMotionEvent_getY)(const AInputEvent* motion_event, size_t pointer_index);

static float hook_AMotionEvent_getAxisValue(const AInputEvent* motion_event, int32_t axis, size_t pointer_index) {
    int32_t source = AInputEvent_getSource(motion_event);
    if (source == AINPUT_SOURCE_MOUSE_RELATIVE) {
        if (axis == AMOTION_EVENT_AXIS_RELATIVE_X) {
            return orig_AMotionEvent_getAxisValue ? orig_AMotionEvent_getAxisValue(motion_event, AMOTION_EVENT_AXIS_RELATIVE_Y, pointer_index) : 0;
        } else if (axis == AMOTION_EVENT_AXIS_RELATIVE_Y) {
            return orig_AMotionEvent_getAxisValue ? orig_AMotionEvent_getAxisValue(motion_event, AMOTION_EVENT_AXIS_RELATIVE_X, pointer_index) : 0;
        }
    }
    return orig_AMotionEvent_getAxisValue ? orig_AMotionEvent_getAxisValue(motion_event, axis, pointer_index) : 0;
}

static float hook_AMotionEvent_getX(const AInputEvent* motion_event, size_t pointer_index) {
    int32_t source = AInputEvent_getSource(motion_event);
    if (source == AINPUT_SOURCE_MOUSE_RELATIVE) {
        return orig_AMotionEvent_getY ? orig_AMotionEvent_getY(motion_event, pointer_index) : 0;
    }
    return orig_AMotionEvent_getX ? orig_AMotionEvent_getX(motion_event, pointer_index) : 0;
}

static float hook_AMotionEvent_getY(const AInputEvent* motion_event, size_t pointer_index) {
    int32_t source = AInputEvent_getSource(motion_event);
    if (source == AINPUT_SOURCE_MOUSE_RELATIVE) {
        return orig_AMotionEvent_getX ? orig_AMotionEvent_getX(motion_event, pointer_index) : 0;
    }
    return orig_AMotionEvent_getY ? orig_AMotionEvent_getY(motion_event, pointer_index) : 0;
}

class PointerFixModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        FILE *fp = fopen("/proc/self/maps", "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                char perms[5];
                unsigned int dev_major, dev_minor;
                ino_t inode;
                char path[256] = {0};
                
                if (sscanf(line, "%*s %4s %*s %x:%x %lu %255s", perms, &dev_major, &dev_minor, &inode, path) >= 5) {
                    if (inode != 0 && perms[0] == 'r' && perms[2] == 'x') {
                        dev_t dev = makedev(dev_major, dev_minor);
                        api->pltHookRegister(dev, inode, "AMotionEvent_getAxisValue", (void*)hook_AMotionEvent_getAxisValue, (void**)&orig_AMotionEvent_getAxisValue);
                        api->pltHookRegister(dev, inode, "AMotionEvent_getX", (void*)hook_AMotionEvent_getX, (void**)&orig_AMotionEvent_getX);
                        api->pltHookRegister(dev, inode, "AMotionEvent_getY", (void*)hook_AMotionEvent_getY, (void**)&orig_AMotionEvent_getY);
                    }
                }
            }
            fclose(fp);
            
            if (api->pltHookCommit()) {
                LOGD("Hooks committed successfully");
            }
        }
    }

private:
    Api *api;
};

REGISTER_ZYGISK_MODULE(PointerFixModule)
