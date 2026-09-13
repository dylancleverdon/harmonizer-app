package com.dylan.harmonizer

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

class HarmonizerViewModel(app: Application) : AndroidViewModel(app) {

    private val store = SettingsStore(app)
    private var handle: Long = 0L
    private var pollJob: Job? = null
    private val metricsBuffer = FloatArray(EngineMetrics.SIZE)

    private val _settings = MutableStateFlow(store.load())
    val settings: StateFlow<HarmonizerSettings> = _settings

    private val _metrics = MutableStateFlow(EngineMetrics())
    val metrics: StateFlow<EngineMetrics> = _metrics

    private val _running = MutableStateFlow(false)
    val running: StateFlow<Boolean> = _running

    private val _status = MutableStateFlow<String?>(null)
    val status: StateFlow<String?> = _status

    private val _inputDevices = MutableStateFlow<List<AudioDeviceOption>>(emptyList())
    val inputDevices: StateFlow<List<AudioDeviceOption>> = _inputDevices

    private val _outputDevices = MutableStateFlow<List<AudioDeviceOption>>(emptyList())
    val outputDevices: StateFlow<List<AudioDeviceOption>> = _outputDevices

    val midi = MidiController(app) { status, d1, d2 ->
        if (handle != 0L) NativeBridge.nativeMidiEvent(handle, status, d1, d2)
    }

    init {
        handle = NativeBridge.nativeCreate()
        pushAllParams()
        midi.start()
        refreshDevices()
    }

    fun refreshDevices() {
        val ctx = getApplication<Application>()
        _inputDevices.value = AudioDevices.inputs(ctx)
        _outputDevices.value = AudioDevices.outputs(ctx)
        midi.refresh()
    }

    // --- transport -----------------------------------------------------------

    fun start() {
        if (handle == 0L || _running.value) return
        val s = _settings.value
        val ok = NativeBridge.nativeStart(handle, s.inputDeviceId, s.outputDeviceId, s.inputPreset)
        _running.value = ok
        _status.value = if (ok) null else
            "Could not open the audio streams. Check that another app is not holding the mic."
        if (ok) startPolling()
    }

    fun stop() {
        if (handle == 0L) return
        NativeBridge.nativeStop(handle)
        _running.value = false
        pollJob?.cancel()
        pollJob = null
    }

    fun panic() {
        if (handle != 0L) NativeBridge.nativeAllNotesOff(handle)
    }

    private fun startPolling() {
        pollJob?.cancel()
        pollJob = viewModelScope.launch {
            while (true) {
                if (handle != 0L) {
                    NativeBridge.nativeGetMetrics(handle, metricsBuffer)
                    _metrics.value = EngineMetrics.from(metricsBuffer)
                    _running.value = NativeBridge.nativeIsRunning(handle)
                }
                delay(100)
            }
        }
    }

    // --- settings ------------------------------------------------------------

    /**
     * Changing the audio device means reopening the streams, so the engine is
     * bounced. Every other setting is picked up live by the audio thread.
     */
    fun update(transform: (HarmonizerSettings) -> HarmonizerSettings) {
        val old = _settings.value
        val next = transform(old)
        _settings.value = next
        store.save(next)
        pushAllParams()

        val needsRestart = next.inputDeviceId != old.inputDeviceId ||
            next.outputDeviceId != old.outputDeviceId ||
            next.inputPreset != old.inputPreset
        if (needsRestart && _running.value) {
            stop()
            start()
        }
    }

    private fun pushAllParams() {
        if (handle == 0L) return
        val s = _settings.value
        val p = NativeBridge.ParamId
        NativeBridge.nativeSetParam(handle, p.QUALITY_MODE, s.qualityMode.id.toFloat())
        NativeBridge.nativeSetParam(handle, p.QUALITY_AMOUNT, s.qualityAmount)
        NativeBridge.nativeSetParam(handle, p.ADAPTIVE_LATENCY, if (s.adaptiveLatency) 1f else 0f)
        NativeBridge.nativeSetParam(handle, p.ADAPTIVE_VOICE_SCALING, if (s.adaptiveVoiceScaling) 1f else 0f)
        NativeBridge.nativeSetParam(handle, p.FORMANT_CORRECTION, if (s.formantCorrection) 1f else 0f)
        NativeBridge.nativeSetParam(handle, p.HARMONY_MODE, s.harmonyMode.id.toFloat())
        NativeBridge.nativeSetParam(handle, p.WET_DRY, s.wetDry)
        NativeBridge.nativeSetParam(handle, p.OUTPUT_GAIN, s.outputGain)
        NativeBridge.nativeSetParam(handle, p.FFT_SIZE, s.fftSize.toFloat())
        NativeBridge.nativeSetParam(handle, p.BYPASS, if (s.bypass) 1f else 0f)
    }

    /** True when the current routing will almost certainly howl. */
    fun feedbackRisk(): Boolean {
        val input = _inputDevices.value.firstOrNull { it.id == _settings.value.inputDeviceId }
        val output = _outputDevices.value.firstOrNull { it.id == _settings.value.outputDeviceId }
        val micIsBuiltIn = input?.isBuiltinMic ?: (_settings.value.inputDeviceId == NativeBridge.DEVICE_UNSPECIFIED)
        val outIsSpeaker = output?.isSpeaker ?: (_settings.value.outputDeviceId == NativeBridge.DEVICE_UNSPECIFIED)
        return micIsBuiltIn && outIsSpeaker
    }

    fun bluetoothOutput(): AudioDeviceOption? =
        _outputDevices.value.firstOrNull { it.id == _settings.value.outputDeviceId && it.isBluetooth }

    override fun onCleared() {
        pollJob?.cancel()
        midi.stop()
        if (handle != 0L) {
            NativeBridge.nativeStop(handle)
            NativeBridge.nativeDestroy(handle)
            handle = 0L
        }
        super.onCleared()
    }
}
