/* PS2-only borrowed positional CHD bridge. SPDX-License-Identifier: MIT */
#include <jni.h>
#include "discimages/ChdImageStream.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {
JavaVM* javaVm = nullptr;
JNIEnv* currentEnv() {
    JNIEnv* env = nullptr;
    if(!javaVm || javaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
        throw std::runtime_error("CHD entry must run on its attached Java thread");
    return env;
}
struct JavaFailure {
    jthrowable original = nullptr;
    ~JavaFailure() { if(original) currentEnv()->DeleteGlobalRef(original); }
    void check(JNIEnv* env) {
        if(!env->ExceptionCheck()) return;
        auto error = env->ExceptionOccurred(); env->ExceptionClear();
        if(!original) original = static_cast<jthrowable>(env->NewGlobalRef(error));
        env->DeleteLocalRef(error);
        throw std::runtime_error("Original CHD source failed");
    }
    bool publish(JNIEnv* env) {
        if(!original) return false;
        env->Throw(original); env->DeleteGlobalRef(original); original = nullptr; return true;
    }
};
// A single borrowed descriptor survives libchdr C callbacks. Never call original.close().
class JavaInput final : public Framework::CStream {
    jobject original = nullptr;
    jmethodID readMethod = nullptr;
    uint64 length = 0, position = 0;
    std::shared_ptr<JavaFailure> failure;
public:
    JavaInput(JNIEnv* env, jobject input, std::shared_ptr<JavaFailure> state) : failure(std::move(state)) {
        auto type = env->GetObjectClass(input); failure->check(env);
        auto sizeMethod = env->GetMethodID(type, "size", "()J");
        if(!sizeMethod) { env->DeleteLocalRef(type); failure->check(env); throw std::runtime_error("Missing original size method"); }
        readMethod = env->GetMethodID(type, "read", "(J[BII)I");
        env->DeleteLocalRef(type); failure->check(env);
        auto size = env->CallLongMethod(input, sizeMethod); failure->check(env);
        if(size < 124 || size > 16LL * 1024 * 1024 * 1024) throw std::runtime_error("Original CHD size outside PS2 budget");
        length = static_cast<uint64>(size);
        original = env->NewGlobalRef(input); failure->check(env);
        if(!original) throw std::runtime_error("Cannot retain borrowed CHD source");
    }
    ~JavaInput() override { if(original) currentEnv()->DeleteGlobalRef(original); }
    uint64 GetLength() override { return length; }
    uint64 Tell() override { return position; }
    bool IsEOF() override { return position >= length; }
    void Seek(int64 displacement, Framework::STREAM_SEEK_DIRECTION origin) override {
        if(failure->original) throw std::runtime_error("Original CHD failure pending");
        uint64 base;
        switch(origin) {
            case Framework::STREAM_SEEK_SET: base = 0; break;
            case Framework::STREAM_SEEK_CUR: base = position; break;
            case Framework::STREAM_SEEK_END: base = length; break;
            default: throw std::runtime_error("Invalid original seek origin");
        }
        if(displacement < 0) {
            uint64 magnitude = static_cast<uint64>(-(displacement + 1)) + 1;
            if(magnitude > base) throw std::runtime_error("Original seek before start");
            position = base - magnitude;
        } else {
            if(static_cast<uint64>(displacement) > length - base) throw std::runtime_error("Original seek beyond source");
            position = base + static_cast<uint64>(displacement);
        }
    }
    uint64 Write(const void*, uint64) override { throw std::runtime_error("Read-only CHD original"); }
    uint64 Read(void* target, uint64 requested) override {
        if(failure->original) throw std::runtime_error("Original CHD failure pending");
        if(!requested) return 0;
        if(!target || requested > std::numeric_limits<size_t>::max()) throw std::runtime_error("Invalid original read buffer");
        auto env = currentEnv();
        uint64 total = std::min(requested, length - position), done = 0;
        auto out = static_cast<uint8*>(target);
        while(done < total) {
            auto count = static_cast<jsize>(std::min<uint64>(64 * 1024, total - done));
            auto bytes = env->NewByteArray(count); failure->check(env);
            if(!bytes) throw std::runtime_error("Cannot allocate bounded CHD read");
            jint read = env->CallIntMethod(original, readMethod, static_cast<jlong>(position), bytes, 0, count);
            if(env->ExceptionCheck()) { env->DeleteLocalRef(bytes); failure->check(env); }
            if(read <= 0 || read > count) { env->DeleteLocalRef(bytes); throw std::runtime_error("Invalid original read progress"); }
            env->GetByteArrayRegion(bytes, 0, read, reinterpret_cast<jbyte*>(out + done));
            env->DeleteLocalRef(bytes); failure->check(env);
            done += static_cast<uint64>(read); position += static_cast<uint64>(read);
        }
        return total;
    }
};
struct Context {
    std::mutex mutex;
    std::shared_ptr<JavaFailure> failure;
    std::unique_ptr<CChdImageStream> stream;
    Context(std::shared_ptr<JavaFailure> state, std::unique_ptr<CChdImageStream> decoded)
        : failure(std::move(state)), stream(std::move(decoded)) {}
};
std::mutex registryMutex;
std::map<jlong, std::shared_ptr<Context>> sessions;
unsigned opening = 0;
jlong nextHandle = 1;
class OpenReservation {
    bool reserved = true;
public:
    OpenReservation() {
        std::lock_guard<std::mutex> lock(registryMutex);
        if(sessions.size() + opening >= 4) throw std::runtime_error("PS2 CHD concurrent decoder budget exceeded");
        ++opening;
    }
    ~OpenReservation() { if(reserved) { std::lock_guard<std::mutex> lock(registryMutex); --opening; } }
    jlong publish(std::shared_ptr<Context> context) {
        std::lock_guard<std::mutex> lock(registryMutex);
        if(nextHandle == std::numeric_limits<jlong>::max()) throw std::runtime_error("CHD session identity exhausted");
        jlong handle = nextHandle++;
        sessions.emplace(handle, std::move(context)); --opening; reserved = false; return handle;
    }
};
std::shared_ptr<Context> find(jlong handle) {
    std::lock_guard<std::mutex> lock(registryMutex);
    auto it = sessions.find(handle);
    if(handle <= 0 || it == sessions.end()) throw std::runtime_error("Closed or foreign PS2 CHD handle");
    return it->second;
}
void ioError(JNIEnv* env, const char* message, const std::shared_ptr<JavaFailure>& failure = {}) {
    if(env->ExceptionCheck() || (failure && failure->publish(env))) return;
    auto type = env->FindClass("java/io/IOException");
    if(type) { env->ThrowNew(type, message); env->DeleteLocalRef(type); }
}
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    javaVm = vm; return JNI_VERSION_1_6;
}
#define ENTRY(name) Java_com_mateussouza_emuorbit_advance_data_storage_playstation_PlayStation2NativeChdSource_##name
extern "C" JNIEXPORT jint JNICALL ENTRY(nativeAbi)(JNIEnv*, jclass) { return 1; }
extern "C" JNIEXPORT jlong JNICALL ENTRY(nativeOpen)(JNIEnv* env, jclass, jobject original, jlong logical, jint unit) {
    std::shared_ptr<JavaFailure> failure;
    try {
        failure = std::make_shared<JavaFailure>();
        if(!original || logical <= 0 || (unit != 2048 && unit != 2448)) throw std::runtime_error("Invalid CHD view geometry");
        OpenReservation reservation;
        // Java already validates the bounded optical inventory and reads only
        // the first data track. Do not reapply retired Play! CDDA/32-bit limits
        // or transform audio to inspect data destined for ARMSX2.
        auto stream = std::make_unique<CChdImageStream>(std::make_unique<JavaInput>(env, original, failure));
        if(failure->original) throw std::runtime_error("Original source failed during decode");
        if(stream->GetLength() != static_cast<uint64>(logical) || stream->GetUnitSize() != static_cast<uint32>(unit))
            throw std::runtime_error("Native CHD geometry differs from Java preflight");
        return reservation.publish(std::make_shared<Context>(failure, std::move(stream)));
    } catch(const std::exception& error) { ioError(env, error.what(), failure); return 0; }
    catch(...) { ioError(env, "Unknown native CHD open failure", failure); return 0; }
}
extern "C" JNIEXPORT jint JNICALL ENTRY(nativeRead)(JNIEnv* env, jclass, jlong handle, jlong at, jbyteArray output, jint offset, jint count) {
    std::shared_ptr<Context> context;
    std::unique_lock<std::mutex> readLock;
    try {
        if(!output || at < 0 || offset < 0 || count < 0 || count > 64 * 1024
            || offset > env->GetArrayLength(output) - count) throw std::runtime_error("Invalid bounded CHD destination");
        context = find(handle);
        readLock = std::unique_lock<std::mutex>(context->mutex, std::try_to_lock);
        if(!readLock.owns_lock()) throw std::runtime_error("Concurrent/reentrant CHD read refused");
        auto size = context->stream->GetLength();
        if(static_cast<uint64>(at) > size || static_cast<uint64>(count) > size - static_cast<uint64>(at))
            throw std::runtime_error("Decoded read outside CHD geometry");
        if(!count) return 0;
        std::vector<uint8> bytes(static_cast<size_t>(count));
        context->stream->Seek(at, Framework::STREAM_SEEK_SET);
        if(context->stream->Read(bytes.data(), count) != static_cast<uint64>(count) || context->failure->original)
            throw std::runtime_error("Short/failed decoded CHD read");
        env->SetByteArrayRegion(output, offset, count, reinterpret_cast<const jbyte*>(bytes.data()));
        if(env->ExceptionCheck()) return 0;
        return count;
    } catch(const std::exception& error) { ioError(env, error.what(), readLock.owns_lock() ? context->failure : nullptr); return 0; }
    catch(...) { ioError(env, "Unknown native CHD read failure", readLock.owns_lock() ? context->failure : nullptr); return 0; }
}
extern "C" JNIEXPORT void JNICALL ENTRY(nativeClose)(JNIEnv* env, jclass, jlong handle) {
    try {
        std::shared_ptr<Context> released;
        {
            std::lock_guard<std::mutex> lock(registryMutex);
            auto it = sessions.find(handle);
            if(handle <= 0 || it == sessions.end()) throw std::runtime_error("Closed or foreign PS2 CHD handle");
            released = std::move(it->second); sessions.erase(it);
        } // Codec/global refs die outside the registry lock; reads keep shared ownership.
    } catch(const std::exception& error) { ioError(env, error.what()); }
    catch(...) { ioError(env, "Unknown native CHD close failure"); }
}
