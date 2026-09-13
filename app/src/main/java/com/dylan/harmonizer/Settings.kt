package com.dylan.harmonizer

import android.content.Context
import android.content.SharedPreferences

enum class QualityMode(val id: Int, val title: String) {
    /** Fewer partials resynthesised per voice. Cheapest per unit of audible loss. */
    VOCODER(2, "Vocoder bands"),
    /** Lower internal sample rate. Window duration is held, so latency does not move. */
    SAMPLE_RATE(1, "Sample rate"),
    /** Quantise the wet path. Honest about what it does and does not buy. */
    BIT_DEPTH(0, "Bit depth");

    companion object {
        fun fromId(id: Int) = entries.firstOrNull { it.id == id } ?: VOCODER
    }
}

enum class HarmonyMode(val id: Int, val title: String) {
    FIXED_INTERVAL(0, "Fixed interval"),
    ABSOLUTE(1, "Absolute pitch");

    companion object {
        fun fromId(id: Int) = entries.firstOrNull { it.id == id } ?: FIXED_INTERVAL
    }
}

data class HarmonizerSettings(
    val qualityMode: QualityMode = QualityMode.VOCODER,
    val qualityAmount: Float = 0.35f,
    val adaptiveLatency: Boolean = false,
    val adaptiveVoiceScaling: Boolean = false,
    val formantCorrection: Boolean = true,
    val harmonyMode: HarmonyMode = HarmonyMode.FIXED_INTERVAL,
    val wetDry: Float = 0.5f,
    val outputGain: Float = 1.0f,
    val fftSize: Int = 1024,
    val bypass: Boolean = false,
    val inputPreset: Int = NativeBridge.InputPreset.UNPROCESSED,
    val inputDeviceId: Int = NativeBridge.DEVICE_UNSPECIFIED,
    val outputDeviceId: Int = NativeBridge.DEVICE_UNSPECIFIED
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
        wetDry = prefs.getFloat(K_WETDRY, 0.5f),
        outputGain = prefs.getFloat(K_GAIN, 1.0f),
        fftSize = prefs.getInt(K_FFT, 1024),
        bypass = false,
        inputPreset = prefs.getInt(K_PRESET, NativeBridge.InputPreset.UNPROCESSED),
        inputDeviceId = prefs.getInt(K_IN_DEV, NativeBridge.DEVICE_UNSPECIFIED),
        outputDeviceId = prefs.getInt(K_OUT_DEV, NativeBridge.DEVICE_UNSPECIFIED)
    )

    fun save(s: HarmonizerSettings) {
        prefs.edit()
            .putInt(K_MODE, s.qualityMode.id)
            .putFloat(K_AMOUNT, s.qualityAmount)
            .putBoolean(K_ADAPT_LAT, s.adaptiveLatency)
            .putBoolean(K_ADAPT_VOICE, s.adaptiveVoiceScaling)
            .putBoolean(K_FORMANT, s.formantCorrection)
            .putInt(K_HARMONY, s.harmonyMode.id)
            .putFloat(K_WETDRY, s.wetDry)
            .putFloat(K_GAIN, s.outputGain)
            .putInt(K_FFT, s.fftSize)
            .putInt(K_PRESET, s.inputPreset)
            .putInt(K_IN_DEV, s.inputDeviceId)
            .putInt(K_OUT_DEV, s.outputDeviceId)
            .apply()
    }

    private companion object {
        const val K_MODE = "qualityMode"
        const val K_AMOUNT = "qualityAmount"
        const val K_ADAPT_LAT = "adaptiveLatency"
        const val K_ADAPT_VOICE = "adaptiveVoiceScaling"
        const val K_FORMANT = "formantCorrection"
        const val K_HARMONY = "harmonyMode"
        const val K_WETDRY = "wetDry"
        const val K_GAIN = "outputGain"
        const val K_FFT = "fftSize"
        const val K_PRESET = "inputPreset"
        const val K_IN_DEV = "inputDeviceId"
        const val K_OUT_DEV = "outputDeviceId"
    }
}
