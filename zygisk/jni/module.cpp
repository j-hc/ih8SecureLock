#include <android/log.h>
#include <errno.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include "parcel.hpp"
#include "zygisk.hpp"

#define LOGD(fmt, ...) \
    __android_log_print(ANDROID_LOG_DEBUG, "ih8SecureLock", "[%d] " fmt, __LINE__, ##__VA_ARGS__)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define ARR_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define STR_LEN(a) (ARR_LEN(a) - 1)

#define LIBBINDER "libbinder.so"

#define FLAG_SECURE 0x00002000

#define I_WINDOW_SESSION_DESC u"android.view.IWindowSession"
#define I_ACTIVITY_TASKMANAGER_DESC u"android.app.IActivityTaskManager"
#define STUB(n) (n "$Stub")
#define TRSCTN(n) ("TRANSACTION_" n)

static uint8_t binder_headers_len;
static uint32_t relayout_code;
static uint32_t relayoutAsync_code;
static uint32_t registerScreenCaptureObserver_code;

static uint32_t getStaticIntFieldJni(JNIEnv* env, const char* cls_name, const char* field_name) {
    jclass cls = env->FindClass(cls_name);
    if (cls == nullptr) {
        env->ExceptionClear();
        LOGD("ERROR getStaticIntFieldJni: Could not get class '%s'", cls_name);
        return 0;
    }
    jfieldID field = env->GetStaticFieldID(cls, field_name, "I");
    if (field == nullptr) {
        env->ExceptionClear();
        LOGD("ERROR getStaticIntFieldJni: Could not get field %s.%s", cls_name, field_name);
        return 0;
    }
    jint val = env->GetStaticIntField(cls, field);
    return val;
}

static bool getTransactionCodes(JNIEnv* env) {
    relayout_code = getStaticIntFieldJni(env, STUB("android/view/IWindowSession"), TRSCTN("relayout"));
    if (relayout_code == 0) return false;

    relayoutAsync_code = getStaticIntFieldJni(env, STUB("android/view/IWindowSession"), TRSCTN("relayoutAsync"));
    if (relayoutAsync_code == 0) return false;

    registerScreenCaptureObserver_code =
        getStaticIntFieldJni(env, STUB("android/app/IActivityTaskManager"), TRSCTN("registerScreenCaptureObserver"));
    if (registerScreenCaptureObserver_code == 0) return false;
    return true;
}

static bool getBinder(ino_t* inode, dev_t* dev) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;
    char mapbuf[256], flags[8];
    while (fgets(mapbuf, sizeof(mapbuf), fp)) {
        unsigned int dev_major, dev_minor;
        int cur = 0;
        sscanf(mapbuf, "%*s %s %*x %x:%x %lu %*s%n", flags, &dev_major, &dev_minor, inode, &cur);
        if (cur < (int)STR_LEN(LIBBINDER)) continue;
        if (memcmp(&mapbuf[cur - STR_LEN(LIBBINDER)], LIBBINDER, STR_LEN(LIBBINDER)) == 0 && flags[2] == 'x') {
            *dev = makedev(dev_major, dev_minor);
            fclose(fp);
            return true;
        }
    }
    fclose(fp);
    return false;
}

int (*transactOrig)(void*, int32_t, uint32_t, void*, void*, uint32_t);

int transactHook(void* self, int32_t handle, uint32_t code, void* pdata, void* preply, uint32_t flags) {
    auto pparcel = (PParcel*)pdata;
    auto parcel = FakeParcel(pparcel->data);

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

        parcel.skipFlatObj();               // IWindow flat obj
        parcel.skip(7 * sizeof(uint32_t));  // x,y,horizontalWeight,verticalWeight,width,height,type
        auto* flags = parcel.peekInt32Ref();
        *flags &= ~FLAG_SECURE;
        LOGD("Bypassed secure lock");

    } else if (code == registerScreenCaptureObserver_code &&
               STR_LEN(I_ACTIVITY_TASKMANAGER_DESC) == descLen &&
               memcmp(desc, I_ACTIVITY_TASKMANAGER_DESC, descLen * sizeof(char16_t)) == 0) {
        // early-return from capture listener
        LOGD("Bypassed screenshot listener");
        return 0;
    }
    return transactOrig(self, handle, code, pdata, preply, flags);
}

static int getSDK() {
    char sdk_str[2];
    if (!__system_property_get("ro.build.version.sdk", sdk_str)) {
        LOGD("ERROR __system_property_get: %s", strerror(errno));
        return 0;
    }
    int sdk = atoi(sdk_str);
    if (sdk == 0) {
        LOGD("ERROR getSDK: could not get SDK '%s'", sdk_str);
        return 0;
    }
    return sdk;
}

static bool hookBinder(zygisk::Api* api) {
    ino_t inode;
    dev_t dev;
    if (!getBinder(&inode, &dev)) {
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

static uint8_t getBinderHeadersLen(int sdk) {
    if (sdk >= 30) return 3 * sizeof(uint32_t);
    else if (sdk == 29) return 2 * sizeof(uint32_t);
    else return 1 * sizeof(uint32_t);
}

static bool run(zygisk::Api* api, JNIEnv* env) {
    int sdk = getSDK();
    if (sdk == 0) return false;
    binder_headers_len = getBinderHeadersLen(sdk);
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

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if (!run(api, env)) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const char* process = env->GetStringUTFChars(args->nice_name, nullptr);
        LOGD("Loaded for %s", process);
        env->ReleaseStringUTFChars(args->nice_name, process);
    }

   private:
    zygisk::Api* api;
    JNIEnv* env;
};

REGISTER_ZYGISK_MODULE(ih8SecureLock)
