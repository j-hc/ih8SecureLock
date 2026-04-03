#include <android/log.h>
#include <jni.h>
#include <string.h>

#include "binder.hpp"
#include "zygisk.hpp"

#define LOGD(fmt, ...) \
    __android_log_print(ANDROID_LOG_DEBUG, "ih8SecureLock", "[%d] [%s] " fmt, __LINE__, PROC_NAME, ##__VA_ARGS__)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define ARR_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define STR_LEN(a) (ARR_LEN(a) - 1)

#define FLAG_SECURE 0x00002000

#define I_WINDOW_SESSION_DESC u"android.view.IWindowSession"
#define I_ACTIVITY_TASKMANAGER_DESC u"android.app.IActivityTaskManager"

static int sdk = 0;
static uint32_t relayout_code = 0;
static uint32_t relayoutAsync_code = 0;
static uint32_t registerScreenCaptureObserver_code = 0;

static const char* PROC_NAME = "";

static bool getTransactionCodes(JNIEnv* env) {
    relayout_code = getStaticIntFieldJni(env, STUB("android/view/IWindowSession"), TRSCTN("relayout"));
    relayoutAsync_code = getStaticIntFieldJni(env, STUB("android/view/IWindowSession"), TRSCTN("relayoutAsync"));
    registerScreenCaptureObserver_code =
        getStaticIntFieldJni(env, STUB("android/app/IActivityTaskManager"), TRSCTN("registerScreenCaptureObserver"));

    if (registerScreenCaptureObserver_code == 0 && relayoutAsync_code == 0 && relayout_code == 0) {
        LOGD("ERROR getTransactionCodes: Could not get any transaction codes");
        return false;
    }
    return true;
}

int (*transactOrig)(void*, int32_t, uint32_t, void*, void*, uint32_t);

int transactHook(void* self, int32_t handle, uint32_t code, void* pdata, void* preply, uint32_t flags) {
    auto pparcel = (PParcel*)pdata;
    auto parcel = FakeParcel(pparcel->data);

    size_t binder_headers_len = getBinderHeadersLen(sdk);
    if (pparcel->data_size < binder_headers_len + 4) {
        return transactOrig(self, handle, code, pdata, preply, flags);
    }
    parcel.skip(binder_headers_len);  // header

    auto descLen = parcel.readInt32();
    auto desc = parcel.readString16(descLen);

    if ((code == relayout_code || code == relayoutAsync_code) &&
        STR_LEN(I_WINDOW_SESSION_DESC) == descLen &&
        memcmp(desc, I_WINDOW_SESSION_DESC, descLen * sizeof(char16_t)) == 0) {
        // remove FLAG_SECURE mask

        parcel.skipFlatObj();                              // IWindow flat obj
        if (sdk <= 30) parcel.skip(1 * sizeof(uint32_t));  // seq
        parcel.skip(4 * sizeof(uint32_t));                 // LayoutParams
        parcel.skip(3 * sizeof(uint32_t));                 // requestedWidth, requestedHeight, viewVisibility

        auto* flags = parcel.peekInt32Ref();
        if (*flags & FLAG_SECURE) {
            *flags &= ~FLAG_SECURE;
            LOGD("Bypassed secure lock");
        }
    } else if (code == registerScreenCaptureObserver_code &&
               STR_LEN(I_ACTIVITY_TASKMANAGER_DESC) == descLen &&
               memcmp(desc, I_ACTIVITY_TASKMANAGER_DESC, descLen * sizeof(char16_t)) == 0) {
        // early-return from capture listener
        LOGD("Bypassed screenshot listener");
        return 0;
    }
    return transactOrig(self, handle, code, pdata, preply, flags);
}

static bool hookBinder(zygisk::Api* api) {
    ino_t inode;
    dev_t dev;
    if (!getMapping("libbinder.so", &inode, &dev)) {
        LOGD("ERROR: Could not get libbinder");
        return false;
    }

    api->pltHookRegister(dev, inode, "_ZN7android14IPCThreadState8transactEijRKNS_6ParcelEPS1_j",
                         (void**)&transactHook, (void**)&transactOrig);
    if (!api->pltHookCommit()) {
        LOGD("ERROR: pltHookCommit");
        return false;
    }
    return true;
}

static bool run(zygisk::Api* api, JNIEnv* env) {
    sdk = android_get_device_api_level();
    if (sdk <= 0) {
        LOGD("ERROR android_get_device_api_level: %d", sdk);
        return false;
    }
    if (!getTransactionCodes(env)) return false;
    if (!hookBinder(api)) return false;
    return true;
}

class ih8SecureLock : public zygisk::ModuleBase {
   public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs* args) override {
        PROC_NAME = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!run(api, env)) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            env->ReleaseStringUTFChars(args->nice_name, PROC_NAME);
            return;
        }
        LOGD("Loaded");
    }

   private:
    zygisk::Api* api;
    JNIEnv* env;
};

REGISTER_ZYGISK_MODULE(ih8SecureLock)
