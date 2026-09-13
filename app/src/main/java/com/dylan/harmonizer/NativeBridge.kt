package com.dylan.harmonizer

/**
 * Thin JNI surface over the C++ engine. Everything below this object runs on the
 * real-time audio thread, so calls here only ever write atomics or push onto a
 * lock-free queue -- none of them block.
 */
object NativeBridge {

    init {
        System.loadLibrary("harmonizer")
    }

    object ParamId {
        const val QUALITY_MODE = 0
        const val QUALITY_AMOUNT = 1
        const val ADAPTIVE_LATENCY = 2
        const val ADAPTIVE_VOICE_SCALING = 3
        const val FORMANT_CORRECTION = 4
        const val HARMONY_MODE = 5
        const val WET_DRY = 6
        const val OUTPUT_GAIN = 7
        const val FFT_SIZE = 8
        const val BYPASS = 9
        const val CHORD_ANCHOR_DEGREE = 10
        const val DOUBLE_ANCHOR = 11
    }

    /** Matches AudioEngine::kUnspecified: let the platform pick the device. */
    const val DEVICE_UNSPECIFIED = -1

    /** Values of oboe::InputPreset that make sense for a live vocal input. */
    object InputPreset {
        const val GENERIC = 1
        const val CAMCORDER = 5
        const val VOICE_RECOGNITION = 6
        const val VOICE_COMMUNICATION = 7
        /** No AGC, no noise suppression, no echo cancellation. Best for singing. */
        const val UNPROCESSED = 9
        const val VOICE_PERFORMANCE = 10
    }

    external fun nativeCreate(): Long
    external fun nativeDestroy(handle: Long)
    external fun nativeStart(
        handle: Long,
        inputDeviceId: Int,
        outputDeviceId: Int,
        inputPreset: Int
    ): Boolean
    external fun nativeStop(handle: Long)
    external fun nativeIsRunning(handle: Long): Boolean
    external fun nativeSetParam(handle: Long, id: Int, value: Float)
    external fun nativeMidiEvent(handle: Long, status: Int, data1: Int, data2: Int)
    external fun nativeAllNotesOff(handle: Long)
    external fun nativeGetMetrics(handle: Long, out: FloatArray)
}

/** Snapshot of what the engine is doing, polled a few times a second for the meters. */
data class EngineMetrics(
    val cpuLoad: Float = 0f,
    val effectiveQuality: Float = 0f,
    val activeVoices: Int = 0,
    val partialsPerVoice: Int = 0,
    val internalSampleRate: Float = 0f,
    val bitDepth: Int = 32,
    val engineLatencyMs: Float = 0f,
    val detectedPitchHz: Float = 0f,
    val inputPeak: Float = 0f,
    val outputPeak: Float = 0f,
    val totalLatencyMs: Float = 0f,
    val xRuns: Int = 0,
    val sampleRate: Int = 48000,
    val burstFrames: Int = 0,
    /** Lowest note of the held chord, or -1. Chord-voicing mode only. */
    val rootNote: Int = -1,
    /** The chord tone the input stands in for. Equals rootNote on fallback. */
    val anchorNote: Int = -1
) {
    companion object {
        const val SIZE = 16

        fun from(v: FloatArray) = EngineMetrics(
            cpuLoad = v[0],
            effectiveQuality = v[1],
            activeVoices = v[2].toInt(),
            partialsPerVoice = v[3].toInt(),
            internalSampleRate = v[4],
            bitDepth = v[5].toInt(),
            engineLatencyMs = v[6],
            detectedPitchHz = v[7],
            inputPeak = v[8],
            outputPeak = v[9],
            totalLatencyMs = v[10],
            xRuns = v[11].toInt(),
            sampleRate = v[12].toInt(),
            burstFrames = v[13].toInt(),
            rootNote = v[14].toInt(),
            anchorNote = v[15].toInt()
        )
    }
}
