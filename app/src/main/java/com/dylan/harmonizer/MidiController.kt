package com.dylan.harmonizer

import android.content.Context
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.Looper
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

data class MidiPort(val id: Int, val name: String, val isUsb: Boolean)

/**
 * Opens a MIDI input port and forwards note messages to the engine.
 *
 * USB-C controllers show up here automatically as long as the phone is acting as
 * USB host; Bluetooth LE controllers appear once they have been paired and
 * connected by the system.
 */
class MidiController(
    private val context: Context,
    private val onEvent: (Int, Int, Int) -> Unit
) {
    private val manager: MidiManager? =
        context.getSystemService(Context.MIDI_SERVICE) as? MidiManager

    private val _ports = MutableStateFlow<List<MidiPort>>(emptyList())
    val ports: StateFlow<List<MidiPort>> = _ports

    private val _connected = MutableStateFlow<String?>(null)
    val connected: StateFlow<String?> = _connected

    private val _lastNote = MutableStateFlow<String?>(null)
    val lastNote: StateFlow<String?> = _lastNote

    /** Notes currently held, ascending. Used to show the chord being voiced. */
    private val _heldNotes = MutableStateFlow<List<Int>>(emptyList())
    val heldNotes: StateFlow<List<Int>> = _heldNotes

    private var openDevice: MidiDevice? = null
    private var openPort: android.media.midi.MidiOutputPort? = null
    private val handler = Handler(Looper.getMainLooper())

    private val deviceCallback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(device: MidiDeviceInfo) = refresh()
        override fun onDeviceRemoved(device: MidiDeviceInfo) = refresh()
    }

    fun start() {
        manager?.registerDeviceCallback(deviceCallback, handler)
        refresh()
    }

    fun stop() {
        manager?.unregisterDeviceCallback(deviceCallback)
        close()
    }

    fun refresh() {
        val mgr = manager ?: return
        // A controller is a device with at least one *output* port -- output from
        // the controller's point of view, which is our input.
        _ports.value = mgr.devices
            .filter { it.outputPortCount > 0 }
            .map { info ->
                val props = info.properties
                val name = props.getString(MidiDeviceInfo.PROPERTY_NAME)
                    ?: props.getString(MidiDeviceInfo.PROPERTY_PRODUCT)
                    ?: "MIDI device ${info.id}"
                MidiPort(info.id, name, info.type == MidiDeviceInfo.TYPE_USB)
            }
    }

    fun open(portId: Int) {
        val mgr = manager ?: return
        val info = mgr.devices.firstOrNull { it.id == portId } ?: return
        close()
        val listener = MidiManager.OnDeviceOpenedListener { device ->
            val port = device?.openOutputPort(0)
            if (device != null && port != null) {
                openDevice = device
                openPort = port
                port.connect(receiver)
                val name = info.properties.getString(MidiDeviceInfo.PROPERTY_NAME)
                    ?: "MIDI device ${info.id}"
                handler.post { _connected.value = name }
            } else {
                runCatching { device?.close() }
                handler.post { _connected.value = null }
            }
        }
        mgr.openDevice(info, listener, handler)
    }

    fun close() {
        openPort?.let { port ->
            runCatching { port.disconnect(receiver) }
            runCatching { port.close() }
        }
        openPort = null
        runCatching { openDevice?.close() }
        openDevice = null
        _connected.value = null
        _heldNotes.value = emptyList()
    }

    private val receiver = object : MidiReceiver() {
        override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
            parser.parse(msg, offset, count)
        }
    }

    private val parser = MidiParser { status, d1, d2 ->
        onEvent(status, d1, d2)
        val cmd = status and 0xF0
        when {
            cmd == 0x90 && d2 > 0 -> {
                _lastNote.value = "${noteName(d1)}  ${centsFromMiddleC(d1)} cents"
                // Mirrors what the engine does with the same bytes, so the chord
                // readout matches what is actually sounding.
                _heldNotes.value = (_heldNotes.value + d1).distinct().sorted()
            }
            cmd == 0x80 || (cmd == 0x90 && d2 == 0) -> {
                _heldNotes.value = _heldNotes.value.filterNot { it == d1 }
            }
            cmd == 0xB0 && (d1 == 123 || d1 == 120) -> {
                _heldNotes.value = emptyList()
            }
        }
    }

    companion object {
        private val NAMES = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

        fun noteName(note: Int): String = "${NAMES[note % 12]}${note / 12 - 1}"

        /** The spec, literally: everything is measured in cents from middle C. */
        fun centsFromMiddleC(note: Int): String {
            val cents = (note - 60) * 100
            return if (cents >= 0) "+$cents" else "$cents"
        }
    }
}

/**
 * Streaming MIDI byte parser. Handles running status and skips the real-time
 * bytes a controller can interleave mid-message.
 */
class MidiParser(private val emit: (Int, Int, Int) -> Unit) {
    private var status = 0
    private var data1 = 0
    private var haveData1 = false

    fun parse(data: ByteArray, offset: Int, count: Int) {
        for (i in offset until offset + count) {
            val b = data[i].toInt() and 0xFF
            when {
                b >= 0xF8 -> { /* real-time: clock, start, stop -- ignore, keep status */ }
                b == 0xF7 || (b in 0xF0..0xF6) -> { status = 0; haveData1 = false }
                b >= 0x80 -> { status = b; haveData1 = false }
                status == 0 -> { /* data byte with no status: nothing to do */ }
                else -> {
                    val cmd = status and 0xF0
                    // Program change and channel pressure carry a single data byte.
                    val oneByte = cmd == 0xC0 || cmd == 0xD0
                    if (oneByte) {
                        emit(status, b, 0)
                    } else if (!haveData1) {
                        data1 = b
                        haveData1 = true
                    } else {
                        emit(status, data1, b)
                        haveData1 = false
                    }
                }
            }
        }
    }
}
