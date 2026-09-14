package com.dylan.harmonizer

import android.content.Context
import android.content.SharedPreferences

/**
 * Declaration order sets the order shown in Settings; `id` crosses the JNI
 * boundary and MUST match `dsp::QualityMode` in Types.h. They were transposed
 * once already, which silently swapped two of the three modes.
 */
enum class QualityMode(val id: Int, val title: String) {
    /** Fewer partials resynthesised per voice. Cheapest per unit of audible loss. */
    VOCODER(2, "Vocoder bands"),
    /** Lower internal sample rate. Window duration is held, so latency does not move. */
    SAMPLE_RATE(0, "Sample rate"),
    /** Quantise the wet path. Honest about what it does and does not buy. */
    BIT_DEPTH(1, "Bit depth");

    companion object {
        fun fromId(id: Int) = entries.firstOrNull { it.id == id } ?: VOCODER
    }
}

enum class HarmonyMode(val id: Int, val title: String) {
    FIXED_INTERVAL(0, "Fixed interval"),
    ABSOLUTE(1, "Absolute pitch"),
    /** You are one tone of the chord; the rest is built around your pitch. */
    CHORD_VOICING(2, "Chord voicing");

    companion object {
        fun fromId(id: Int) = entries.firstOrNull { it.id == id } ?: FIXED_INTERVAL
    }
}

/**
 * Which tone of the held chord your own instrument is standing in for.
 *
 * When the chord does not contain the chosen degree the engine falls back to the
 * root, so an unexpected chord shape still produces a usable harmony rather than
 * silence.
 */
enum class ChordDegree(val degree: Int, val title: String, val detail: String) {
    ROOT(1, "Root", "The chord is built upward from your note."),
    THIRD(3, "3rd", "Major third, or minor if that is what is held."),
    FIFTH(5, "5th", "Perfect, or diminished/augmented if that is what is held."),
    SEVENTH(7, "7th", "Dominant seventh, or major seventh if that is what is held."),
    NINTH(9, "9th", "Ninth, or flat ninth."),
    ELEVENTH(11, "11th", "Eleventh."),
    THIRTEENTH(13, "13th", "Thirteenth.");

    companion object {
        fun fromDegree(d: Int) = entries.firstOrNull { it.degree == d } ?: ROOT
    }
}

/**
 * Rate the audio streams are opened at. Lowering it makes every stage handle
 * proportionally fewer samples per second while the callback deadline in
 * milliseconds stays where it was, which is what buys headroom against dropouts.
 *
 * The cost is bandwidth -- and, because the analysis window is a fixed number of
 * samples, a longer window in milliseconds. Drop the window size alongside the
 * rate to hold latency steady; the Settings screen shows the resulting figure.
 */
enum class StreamRate(val hz: Int, val title: String) {
    DEVICE(NativeBridge.RATE_DEVICE_DEFAULT, "Device"),
    HZ_48000(48000, "48 kHz"),
    HZ_44100(44100, "44.1 kHz"),
    HZ_32000(32000, "32 kHz"),
    HZ_24000(24000, "24 kHz"),
    HZ_16000(16000, "16 kHz");

    companion object {
        fun fromHz(hz: Int) = entries.firstOrNull { it.hz == hz } ?: DEVICE
    }
}

data class HarmonizerSettings(
    val qualityMode: QualityMode = QualityMode.VOCODER,
    val qualityAmount: Float = 0.35f,
    val adaptiveLatency: Boolean = false,
    val adaptiveVoiceScaling: Boolean = false,
    val formantCorrection: Boolean = true,
    val harmonyMode: HarmonyMode = HarmonyMode.FIXED_INTERVAL,
    val chordDegree: ChordDegree = ChordDegree.ROOT,
    val doubleAnchor: Boolean = false,
    val wetDry: Float = 0.5f,
    val outputGain: Float = 1.0f,
    val fftSize: Int = 1024,
    val bypass: Boolean = false,
    val inputPreset: Int = NativeBridge.InputPreset.UNPROCESSED,
    val inputDeviceId: Int = NativeBridge.DEVICE_UNSPECIFIED,
    val outputDeviceId: Int = NativeBridge.DEVICE_UNSPECIFIED,
    val streamRate: StreamRate = StreamRate.DEVICE,
    /** Output buffer as a multiple of one burst: 1 is tightest, 4 is safest. */
    val bufferBursts: Int = 2
)

class SettingsStore(context: Context) {
    private val prefs: SharedPreferences =
        context.getSharedPreferences("harmonizer", Context.MODE_PRIVATE)

    fun load() = HarmonizerSettings(
        qualityMode = QualityMode.fromId(prefs.getInt(K_MODE, QualityMode.VOCODER.id)),
        qualityAmount = prefs.getFloat(K_AMOUNT, 0.35f),
        adaptiveLatency = prefs.getBoolean(K_ADAPT_LAT, false),
        adaptiveVoiceScaling = prefs.getBoolean(K_ADAPT_VOICE, false),
        formantCorrection = prefs.getBoolean(K_FORMANT, true),
        harmonyMode = HarmonyMode.fromId(prefs.getInt(K_HARMONY, 0)),
        chordDegree = ChordDegree.fromDegree(prefs.getInt(K_DEGREE, 1)),
        doubleAnchor = prefs.getBoolean(K_DOUBLE_ANCHOR, false),
        wetDry = prefs.getFloat(K_WETDRY, 0.5f),
        outputGain = prefs.getFloat(K_GAIN, 1.0f),
        fftSize = prefs.getInt(K_FFT, 1024),
        bypass = false,
        inputPreset = prefs.getInt(K_PRESET, NativeBridge.InputPreset.UNPROCESSED),
        inputDeviceId = prefs.getInt(K_IN_DEV, NativeBridge.DEVICE_UNSPECIFIED),
        outputDeviceId = prefs.getInt(K_OUT_DEV, NativeBridge.DEVICE_UNSPECIFIED),
        streamRate = StreamRate.fromHz(prefs.getInt(K_RATE, NativeBridge.RATE_DEVICE_DEFAULT)),
        bufferBursts = prefs.getInt(K_BURSTS, 2)
    )

    fun save(s: HarmonizerSettings) {
        prefs.edit()
            .putInt(K_MODE, s.qualityMode.id)
            .putFloat(K_AMOUNT, s.qualityAmount)
            .putBoolean(K_ADAPT_LAT, s.adaptiveLatency)
            .putBoolean(K_ADAPT_VOICE, s.adaptiveVoiceScaling)
            .putBoolean(K_FORMANT, s.formantCorrection)
            .putInt(K_HARMONY, s.harmonyMode.id)
            .putInt(K_DEGREE, s.chordDegree.degree)
            .putBoolean(K_DOUBLE_ANCHOR, s.doubleAnchor)
            .putFloat(K_WETDRY, s.wetDry)
            .putFloat(K_GAIN, s.outputGain)
            .putInt(K_FFT, s.fftSize)
            .putInt(K_PRESET, s.inputPreset)
            .putInt(K_IN_DEV, s.inputDeviceId)
            .putInt(K_OUT_DEV, s.outputDeviceId)
            .putInt(K_RATE, s.streamRate.hz)
            .putInt(K_BURSTS, s.bufferBursts)
            .apply()
    }

    private companion object {
        const val K_MODE = "qualityMode"
        const val K_AMOUNT = "qualityAmount"
        const val K_ADAPT_LAT = "adaptiveLatency"
        const val K_ADAPT_VOICE = "adaptiveVoiceScaling"
        const val K_FORMANT = "formantCorrection"
        const val K_HARMONY = "harmonyMode"
        const val K_DEGREE = "chordDegree"
        const val K_DOUBLE_ANCHOR = "doubleAnchor"
        const val K_WETDRY = "wetDry"
        const val K_GAIN = "outputGain"
        const val K_FFT = "fftSize"
        const val K_PRESET = "inputPreset"
        const val K_IN_DEV = "inputDeviceId"
        const val K_OUT_DEV = "outputDeviceId"
        const val K_RATE = "streamRate"
        const val K_BURSTS = "bufferBursts"
    }
}
