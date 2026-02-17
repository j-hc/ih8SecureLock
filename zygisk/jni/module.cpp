#include <android/log.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include "aidl_codes.h"
#include "parcel.hpp"
#include "zygisk.hpp"

#define LOGD(fmt, ...) \
    __android_log_print(ANDROID_LOG_DEBUG, "ih8SecureLock", "[%d] " fmt, __LINE__, ##__VA_ARGS__)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define FLAG_SECURE 0x00002000

#define IWINDOWSESSION_DESC u"android.view.IWindowSession"
#define IACTIVITYTASKMANAGER_DESC u"android.app.IActivityTaskManager"

#define LIBBINDER "libbinder.so"

static AIDLCodes_t AIDLCodes;
static uint8_t headers_len;

static bool getAIDLCodes(int sdk) {
    if (sdk >= 30) headers_len = 3 * sizeof(uint32_t);
    else if (sdk == 29) headers_len = 2 * sizeof(uint32_t);
    else headers_len = 1 * sizeof(uint32_t);

    if (sdk - 29 >= (int)ARR_LEN(aidl_codes_tbl)) {
        LOGD("<A10 not supported");
        return false;
    }
    AIDLCodes = aidl_codes_tbl[sdk - 29];
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

bool matchAIDLCode(uint8_t quad[4], uint8_t code) {
    for (int i = 0; i < 4; i++) {
        if (quad[i] == 0) return false;
        if (quad[i] == code) return true;
    }
    return false;
}

int (*transactOrig)(void*, int32_t, uint32_t, void*, void*, uint32_t);

int transactHook(void* self, int32_t handle, uint32_t code, void* pdata, void* preply, uint32_t flags) {
    auto pparcel = (PParcel*)pdata;
    auto parcel = FakeParcel(pparcel->data);

    if (pparcel->data_size < headers_len + 4) goto out;
    parcel.skip(headers_len);  // header

    if ((matchAIDLCode(AIDLCodes.relayout_code, code) || matchAIDLCode(AIDLCodes.relayoutAsync_code, code)) &&
        parcel.checkInterface(IWINDOWSESSION_DESC, STR_LEN(IWINDOWSESSION_DESC))) {
        // remove FLAG_SECURE

        parcel.skipFlatObj();               // IWindow flat obj
        parcel.skip(7 * sizeof(uint32_t));  // x,y,horizontalWeight,verticalWeight,width,height,type
        auto* flags = parcel.peekInt32Ref();
        *flags &= ~FLAG_SECURE;

        LOGD("bypassed secure lock");
    } else if (matchAIDLCode(AIDLCodes.registerScreenCaptureObserver_code, code) &&
               parcel.checkInterface(IACTIVITYTASKMANAGER_DESC, STR_LEN(IACTIVITYTASKMANAGER_DESC))) {
        // early-return from capture listener
        LOGD("bypassed screenshot listener");
        return 0;
    }

out:
    return transactOrig(self, handle, code, pdata, preply, flags);
}

static int getSDK() {
    char sdk_str[2];
    if (!__system_property_get("ro.build.version.sdk", sdk_str)) {
        LOGD("ERROR __system_property_get: %s", strerror(errno));
        return 0;
    }
    return atoi(sdk_str);
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

class ih8SecureLock : public zygisk::ModuleBase {
   public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    bool run() {
        int sdk = getSDK();

        if (sdk == 0) return false;
        if (!getAIDLCodes(sdk)) return false;
        if (!hookBinder(this->api)) return false;
        return true;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if (!run()) {
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
