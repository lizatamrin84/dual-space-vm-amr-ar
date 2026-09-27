#include <jni.h>
#include <string>
#include <dlfcn.h>
#include <android/log.h>

#define LOG_TAG "NativeAbiBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

namespace {

/**
 * Returns the architecture compiled into this specific binary slice.
 */
const char* getCompiledArchitecture() {
#if defined(__aarch64__)
    return "arm64-v8a";
#elif defined(__arm__)
    return "armeabi-v7a";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

/**
 * Checks if the running process is 64-bit or 32-bit.
 */
bool is64BitRuntime() {
    return sizeof(void*) == 8;
}

} // namespace

extern "C" {

/**
 * Returns the active compiled native ABI of the running .so file.
 */
JNIEXPORT jstring JNICALL
Java_com_app_dualspace_bridge_AbiBridge_getNativeCompiledAbi(JNIEnv *env, jobject /* this */) {
    return env->NewStringUTF(getCompiledArchitecture());
}

/**
 * Returns true if the current native process runtime is 64-bit.
 */
JNIEXPORT jboolean JNICALL
Java_com_app_dualspace_bridge_AbiBridge_isProcess64Bit(JNIEnv * /* env */, jobject /* this */) {
    return static_cast<jboolean>(is64BitRuntime());
}

/**
 * Safely loads a shared library (.so) from an absolute file path.
 *
 * @param env JNI environment pointer.
 * @param libPath Absolute path to the .so file.
 * @return 64-bit integer handle representing the dlopen pointer (0 if failed).
 */
JNIEXPORT jlong JNICALL
Java_com_app_dualspace_bridge_AbiBridge_nativeLoadLibrary(
        JNIEnv *env,
        jobject /* this */,
        jstring libPath) {

    if (libPath == nullptr) {
        LOGE("nativeLoadLibrary: libPath is null");
        return 0;
    }

    const char *nativePath = env->GetStringUTFChars(libPath, nullptr);
    if (nativePath == nullptr) {
        LOGE("nativeLoadLibrary: Failed to extract path characters");
        return 0;
    }

    // Clear previous dynamic linker errors
    dlerror();

    // RTLD_NOW: Immediate resolution of symbols
    // RTLD_LOCAL: Loaded symbols are not made available to subsequently loaded libraries
    void *handle = dlopen(nativePath, RTLD_NOW | RTLD_LOCAL);

    if (!handle) {
        const char *errorMsg = dlerror();
        LOGE("dlopen failed for '%s': %s", nativePath, errorMsg ? errorMsg : "Unknown error");
        env->ReleaseStringUTFChars(libPath, nativePath);
        return 0;
    }

    LOGI("Successfully loaded dynamic library: %s [Handle: %p]", nativePath, handle);
    env->ReleaseStringUTFChars(libPath, nativePath);

    // Cast pointer safely to jlong (retains pointer integrity on both 32-bit and 64-bit)
    return reinterpret_cast<jlong>(handle);
}

/**
 * Resolves a function pointer symbol inside a previously opened library handle.
 *
 * @param handle The memory handle returned by nativeLoadLibrary.
 * @param symbolName The exported C symbol name to look up.
 * @return 64-bit pointer address of the resolved symbol (0 if not found).
 */
JNIEXPORT jlong JNICALL
Java_com_app_dualspace_bridge_AbiBridge_nativeFindSymbol(
        JNIEnv *env,
        jobject /* this */,
        jlong handle,
        jstring symbolName) {

    if (handle == 0) {
        LOGE("nativeFindSymbol: Invalid library handle (0)");
        return 0;
    }

    if (symbolName == nullptr) {
        LOGE("nativeFindSymbol: symbolName is null");
        return 0;
    }

    const char *nativeSymbol = env->GetStringUTFChars(symbolName, nullptr);
    if (nativeSymbol == nullptr) {
        return 0;
    }

    void *libHandle = reinterpret_cast<void *>(handle);

    // Clear existing linker errors
    dlerror();

    void *symAddr = dlsym(libHandle, nativeSymbol);
    const char *errorMsg = dlerror();

    if (errorMsg != nullptr) {
        LOGW("dlsym failed for symbol '%s': %s", nativeSymbol, errorMsg);
        env->ReleaseStringUTFChars(symbolName, nativeSymbol);
        return 0;
    }

    LOGI("Symbol '%s' resolved at memory address: %p", nativeSymbol, symAddr);
    env->ReleaseStringUTFChars(symbolName, nativeSymbol);

    return reinterpret_cast<jlong>(symAddr);
}

/**
 * Closes and unloads a dynamic library handle.
 *
 * @param handle The handle obtained from nativeLoadLibrary.
 * @return true if successfully closed, false otherwise.
 */
JNIEXPORT jboolean JNICALL
Java_com_app_dualspace_bridge_AbiBridge_nativeCloseLibrary(
        JNIEnv * /* env */,
        jobject /* this */,
        jlong handle) {

    if (handle == 0) {
        return JNI_FALSE;
    }

    void *libHandle = reinterpret_cast<void *>(handle);
    int result = dlclose(libHandle);

    if (result != 0) {
        const char *errorMsg = dlerror();
        LOGE("dlclose failed on handle %p: %s", libHandle, errorMsg ? errorMsg : "Unknown error");
        return JNI_FALSE;
    }

    LOGI("Library handle %p successfully closed.", libHandle);
    return JNI_TRUE;
}

} // extern "C"