// Android: the few things the program asks of the app's activity (tools/android/java,
// GameActivity: static methods called through JNI): the folder the player chose for their ROMs,
// and Android's folder picker (with the question for access to files before it).
#pragma once
#ifdef __ANDROID__
#include <string>

#include <jni.h>
#include <SDL.h>
#include <SDL_system.h>

namespace gbaw
{

namespace Android
{

// The activity's class (GameActivity), found through the activity itself: a thread SDL made
// can't find the app's own classes by name.
inline jclass ActivityClass(JNIEnv* env)
{
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!activity) return nullptr;
    jclass c = env->GetObjectClass(activity);
    env->DeleteLocalRef(activity);
    return c;
}

inline std::string RomFolder()
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jclass c = env ? ActivityClass(env) : nullptr;
    if (!c) return "";
    std::string out;
    jmethodID m = env->GetStaticMethodID(c, "romFolder", "()Ljava/lang/String;");
    if (m)
    {
        jstring s = (jstring)env->CallStaticObjectMethod(c, m);
        if (s)
        {
            const char* u = env->GetStringUTFChars(s, nullptr);
            out = u ? u : "";
            if (u) env->ReleaseStringUTFChars(s, u);
            env->DeleteLocalRef(s);
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(c);
    return out;
}

// A static method of GameActivity that takes nothing and returns a boolean (or nothing).
inline bool Call(const char* name, bool returnsBool)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jclass c = env ? ActivityClass(env) : nullptr;
    if (!c) return false;
    bool r = false;
    jmethodID m = env->GetStaticMethodID(c, name, returnsBool ? "()Z" : "()V");
    if (m)
    {
        if (returnsBool) r = env->CallStaticBooleanMethod(c, m);
        else env->CallStaticVoidMethod(c, m);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(c);
    return r;
}

inline void ChooseFolder() { Call("chooseFolder", false); }
inline bool Picking() { return Call("picking", true); }

}

} // namespace gbaw
#endif
