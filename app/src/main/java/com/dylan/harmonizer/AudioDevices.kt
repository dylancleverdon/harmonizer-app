package com.dylan.harmonizer

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager

data class AudioDeviceOption(
    val id: Int,
    val label: String,
    val kind: Kind
) {
    enum class Kind { BUILTIN_MIC, WIRED, USB, BLUETOOTH, SPEAKER, OTHER, AUTOMATIC }

    /** Bluetooth output adds 150-300 ms that no amount of DSP tuning can recover. */
    val isBluetooth get() = kind == Kind.BLUETOOTH

    /** Built-in mic into the loudspeaker is a feedback loop waiting to happen. */
    val isSpeaker get() = kind == Kind.SPEAKER
    val isBuiltinMic get() = kind == Kind.BUILTIN_MIC
}

/**
 * Enumerates what is actually plugged in, so input and output can be chosen
 * independently rather than taking whatever the system routes by default.
 */
object AudioDevices {

    private val AUTOMATIC = AudioDeviceOption(
        NativeBridge.DEVICE_UNSPECIFIED, "Automatic (system default)", AudioDeviceOption.Kind.AUTOMATIC
    )

    fun inputs(context: Context): List<AudioDeviceOption> =
        listOf(AUTOMATIC) + devices(context, AudioManager.GET_DEVICES_INPUTS)

    fun outputs(context: Context): List<AudioDeviceOption> =
        listOf(AUTOMATIC) + devices(context, AudioManager.GET_DEVICES_OUTPUTS)

    private fun devices(context: Context, flags: Int): List<AudioDeviceOption> {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        return am.getDevices(flags)
            .filter { it.type != AudioDeviceInfo.TYPE_TELEPHONY }
            .map { info ->
                val product = info.productName?.toString()?.trim().orEmpty()
                val typeLabel = label(info.type)
                val label = when {
                    product.isEmpty() -> typeLabel
                    // Most built-in endpoints report the phone's model as the
                    // product name, which is noise next to "Built-in mic".
                    product.equals(typeLabel, ignoreCase = true) -> typeLabel
                    kind(info.type) == AudioDeviceOption.Kind.USB ||
                        kind(info.type) == AudioDeviceOption.Kind.BLUETOOTH -> "$product ($typeLabel)"
                    else -> typeLabel
                }
                AudioDeviceOption(info.id, label, kind(info.type))
            }
            .distinctBy { it.label }
    }

    private fun kind(type: Int): AudioDeviceOption.Kind = when (type) {
        AudioDeviceInfo.TYPE_BUILTIN_MIC -> AudioDeviceOption.Kind.BUILTIN_MIC
        AudioDeviceInfo.TYPE_BUILTIN_SPEAKER,
        AudioDeviceInfo.TYPE_BUILTIN_SPEAKER_SAFE -> AudioDeviceOption.Kind.SPEAKER
        AudioDeviceInfo.TYPE_WIRED_HEADSET,
        AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> AudioDeviceOption.Kind.WIRED
        AudioDeviceInfo.TYPE_USB_DEVICE,
        AudioDeviceInfo.TYPE_USB_HEADSET,
        AudioDeviceInfo.TYPE_USB_ACCESSORY -> AudioDeviceOption.Kind.USB
        AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
        AudioDeviceInfo.TYPE_BLE_HEADSET,
        AudioDeviceInfo.TYPE_BLE_SPEAKER -> AudioDeviceOption.Kind.BLUETOOTH
        else -> AudioDeviceOption.Kind.OTHER
    }

    private fun label(type: Int): String = when (type) {
        AudioDeviceInfo.TYPE_BUILTIN_MIC -> "Built-in mic"
        AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> "Phone speaker"
        AudioDeviceInfo.TYPE_BUILTIN_SPEAKER_SAFE -> "Phone speaker"
        AudioDeviceInfo.TYPE_WIRED_HEADSET -> "Wired headset"
        AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> "Wired headphones"
        AudioDeviceInfo.TYPE_USB_DEVICE -> "USB audio"
        AudioDeviceInfo.TYPE_USB_HEADSET -> "USB headset"
        AudioDeviceInfo.TYPE_USB_ACCESSORY -> "USB accessory"
        AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> "Bluetooth"
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "Bluetooth (SCO)"
        AudioDeviceInfo.TYPE_BLE_HEADSET -> "Bluetooth LE"
        AudioDeviceInfo.TYPE_BLE_SPEAKER -> "Bluetooth LE speaker"
        AudioDeviceInfo.TYPE_HDMI -> "HDMI"
        AudioDeviceInfo.TYPE_DOCK -> "Dock"
        else -> "Audio device"
    }
}
