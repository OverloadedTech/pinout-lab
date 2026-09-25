#include "lab.hpp"
#include <android/asset_manager_jni.h>
#include <jni.h>
namespace {
std::string string(JNIEnv* e,jstring s) {
    const char* text=e->GetStringUTFChars(s,nullptr);std::string result=text;e->ReleaseStringUTFChars(s,text);return result;
}
}
extern "C" JNIEXPORT jstring JNICALL Java_dev_mediocre_lab_LabBridge_init(JNIEnv* e,jclass,jobject assets,jstring files,jstring hash) {
    std::string error=mlab::install(AAssetManager_fromJava(e,assets),string(e,files),string(e,hash));
    return e->NewStringUTF(error.c_str());
}
extern "C" JNIEXPORT void JNICALL Java_dev_mediocre_lab_LabBridge_command(JNIEnv* e,jclass,jstring text) { mlab::queue(string(e,text)); }
extern "C" JNIEXPORT jstring JNICALL Java_dev_mediocre_lab_LabBridge_snapshot(JNIEnv* e,jclass) {
    std::string text=mlab::published();return e->NewStringUTF(text.c_str());
}
extern "C" JNIEXPORT jstring JNICALL Java_dev_mediocre_lab_LabBridge_snapshotUi(JNIEnv* e,jclass) {
    std::string text=mlab::publishedUi();return e->NewStringUTF(text.c_str());
}
extern "C" JNIEXPORT void JNICALL Java_dev_mediocre_lab_LabBridge_inputRegions(JNIEnv* e,jclass,jfloatArray rectangles,jboolean exclusive) {
    int count=e->GetArrayLength(rectangles);if(count<0||count>128||count%4)return;
    std::vector<float> values(count);e->GetFloatArrayRegion(rectangles,0,count,values.data());
    mlab::inputRegions(std::move(values),exclusive);
}
