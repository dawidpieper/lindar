#pragma once

typedef int jint;
typedef unsigned char jboolean;
typedef void *jobject;
typedef jobject jclass;
typedef const struct JNINativeInterface_ *JNIEnv;
typedef const struct JNIInvokeInterface_ *JavaVM;

#define JNI_OK 0
#define JNI_VERSION_1_6 0x00010006

struct JNINativeInterface_ {
    jboolean (*ExceptionCheck)(JNIEnv *);
    void (*ExceptionClear)(JNIEnv *);
    jclass (*FindClass)(JNIEnv *, const char *);
    jboolean (*IsInstanceOf)(JNIEnv *, jobject, jclass);
    void (*DeleteLocalRef)(JNIEnv *, jobject);
};

struct JNIInvokeInterface_ {
    jint (*GetEnv)(JavaVM *, void **, jint);
};
