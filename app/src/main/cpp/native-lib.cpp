// JNI bridge. Kotlin owns the UI and device enumeration; everything below the
// bridge is real-time C++. Nothing here is called from the audio thread.

#include <jni.h>
#include <memory>

#include "AudioEngine.h"

namespace {

AudioEngine* engineFrom(jlong handle) {
    return reinterpret_cast<AudioEngine*>(handle);
}

// Must match ParamId in NativeBridge.kt.
enum ParamId : jint {
    kQualityMode = 0,
    kQualityAmount = 1,
    kAdaptiveLatency = 2,
    kAdaptiveVoiceScaling = 3,
    kFormantCorrection = 4,
    kHarmonyMode = 5,
    kWetDry = 6,
    kOutputGain = 7,
    kFftSize = 8,
    kBypass = 9,
    kChordAnchorDegree = 10,
    kDoubleAnchor = 11,
};

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_dylan_harmonizer_NativeBridge_nativeCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(new AudioEngine());
}

JNIEXPORT void JNICALL
Java_com_dylan_harmonizer_NativeBridge_nativeDestroy(JNIEnv*, jobject, jlong handle) {
    delete engineFrom(handle);
}

JNIEXPORT jboolean JNICALL
Java_com_dylan_harmonizer_NativeBridge_nativeStart(JNIEnv*, jobject, jlong handle,
                                                    jint inputDeviceId, jint outputDeviceId,
                                                    jint inputPreset, jint sampleRate,
                                                    jint bufferBursts) {
    AudioEngine* e = engineFrom(handle);
    if (!e) return JNI_FALSE;
    return e->start(inputDeviceId, outputDeviceId, inputPreset, sampleRate, bufferBursts)
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_dylan_harmonizer_NativeBridge_nativeStop(JNIEnv*, jobject, jlong handle) {
    if (AudioEngine* e = engineFrom(handle)) e->stop();
}

JNIEXPORT jboolean JNICALL
Java_com_dylan_harmonizer_NativeBridge_nativeIsRunning(JNIEnv*, jobject, jlong handle) {
    AudioEngine* e = engineFrom(handle);
    return (e && e->isRunning()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_dylan_harmonizer_NativeBridge_nativeSetParam(JNIEnv*, jobject, jlong handle,
                                                       jint id, jfloat value) {
    AudioEngine* e = engineFrom(handle);
    if (!e) return;
    dsp::Params& p = e->harmonizer().params();
    switch (id) {
        case kQualityMode:          p.qualityMode.store(static_cast<int>(value)); break;
        case kQualityAmount:        p.qualityAmount.store(value); break;
        case kAdaptiveLatency:      p.adaptiveLatency.store(value > 0.5f); break;
        case kAdaptiveVoiceScaling: p.adaptiveVoiceScaling.store(value > 0.5f); break;
        case kFormantCorrection:    p.formantCorrection.store(value > 0.5f); break;
        case kHarmonyMode:          p.harmonyMode.store(static_cast<int>(value)); break;
        case kWetDry:               p.wetDry.store(value); break;
        case kOutputGain:           p.outputGain.store(value); break;
        case kFftSize:              p.fftSize.store(static_cast<int>(value)); break;
        case kBypass:               p.bypass.store(value > 0.5f); break;
        case kChordAnchorDegree:    p.chordAnchorDegree.store(static_cast<int>(value)); break;
        case kDoubleAnchor:         p.doubleAnchor.store(value > 0.5f); break;
        default: break;
    }
}

// Raw MIDI bytes straight from the Android MIDI receiver. Parsing and voice
// allocation happen on the audio thread via a lock-free queue, so this returns
// immediately and never blocks the MIDI thread.
JNIEXPORT void JNICALL
Java_com_dylan_harmonizer_NativeBridge_nativeMidiEvent(JNIEnv*, jobject, jlong handle,
                                                        jint status, jint data1, jint data2) {
    AudioEngine* e = engineFrom(handle);
    if (!e) return;
    dsp::MidiEvent ev;
    ev.status = static_cast<uint8_t>(status);
    ev.data1 = static_cast<uint8_t>(data1);
    ev.data2 = static_cast<uint8_t>(data2);
    e->harmonizer().midiQueue().push(ev);
}

JNIEXPORT void JNICALL
Java_com_dylan_harmonizer_NativeBridge_nativeAllNotesOff(JNIEnv*, jobject, jlong handle) {
    if (AudioEngine* e = engineFrom(handle)) e->harmonizer().allNotesOff();
}

// Fills a float[17] rather than allocating an object per poll; the UI reads
// this a few times a second.
JNIEXPORT void JNICALL
Java_com_dylan_harmonizer_NativeBridge_nativeGetMetrics(JNIEnv* env, jobject, jlong handle,
                                                         jfloatArray outArray) {
    AudioEngine* e = engineFrom(handle);
    if (!e || outArray == nullptr) return;
    if (env->GetArrayLength(outArray) < 17) return;

    const dsp::Metrics m = e->harmonizer().metrics();
    jfloat v[17];
    v[0]  = m.cpuLoad;
    v[1]  = m.effectiveQuality;
    v[2]  = static_cast<jfloat>(m.activeVoices);
    v[3]  = static_cast<jfloat>(m.partialsPerVoice);
    v[4]  = m.internalSampleRate;
    v[5]  = static_cast<jfloat>(m.bitDepth);
    v[6]  = m.algorithmicLatencyMs;
    v[7]  = m.detectedPitchHz;
    v[8]  = m.inputPeak;
    v[9]  = m.outputPeak;
    v[10] = e->totalLatencyMs();
    v[11] = static_cast<jfloat>(e->xRuns());
    v[12] = static_cast<jfloat>(e->actualSampleRate());
    v[13] = static_cast<jfloat>(e->burstFrames());
    v[14] = static_cast<jfloat>(m.rootNote);
    v[15] = static_cast<jfloat>(m.anchorNote);
    v[16] = static_cast<jfloat>(e->bufferFrames());
    env->SetFloatArrayRegion(outArray, 0, 17, v);
}

}  // extern "C"
