package com.dylan.harmonizer.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.dylan.harmonizer.AudioDeviceOption
import com.dylan.harmonizer.HarmonizerViewModel
import com.dylan.harmonizer.ChordDegree
import com.dylan.harmonizer.HarmonyMode
import com.dylan.harmonizer.MidiController
import com.dylan.harmonizer.MidiPort

@Composable
fun HarmonizerApp(
    vm: HarmonizerViewModel,
    hasMicPermission: Boolean,
    onRequestPermission: () -> Unit
) {
    var showSettings by remember { mutableStateOf(false) }
    Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        if (showSettings) {
            SettingsScreen(vm = vm, onBack = { showSettings = false })
        } else {
            MainScreen(
                vm = vm,
                hasMicPermission = hasMicPermission,
                onRequestPermission = onRequestPermission,
                onOpenSettings = { showSettings = true }
            )
        }
    }
}

@Composable
fun MainScreen(
    vm: HarmonizerViewModel,
    hasMicPermission: Boolean,
    onRequestPermission: () -> Unit,
    onOpenSettings: () -> Unit
) {
    val settings by vm.settings.collectAsState()
    val metrics by vm.metrics.collectAsState()
    val running by vm.running.collectAsState()
    val status by vm.status.collectAsState()
    val inputs by vm.inputDevices.collectAsState()
    val outputs by vm.outputDevices.collectAsState()
    val midiPorts by vm.midi.ports.collectAsState()
    val midiConnected by vm.midi.connected.collectAsState()
    val lastNote by vm.midi.lastNote.collectAsState()
    val heldNotes by vm.midi.heldNotes.collectAsState()

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("Harmonizer", style = MaterialTheme.typography.headlineSmall)
            TextButton(onClick = onOpenSettings) { Text("Settings") }
        }

        if (!hasMicPermission) {
            Warning("Microphone access is needed before any audio can be processed.")
            Button(onClick = onRequestPermission) { Text("Grant microphone access") }
        }

        status?.let { Warning(it) }

        if (vm.feedbackRisk()) {
            Warning(
                "Built-in mic into the phone speaker will feed back. Use headphones, " +
                    "or pick a different output below."
            )
        }
        vm.bluetoothOutput()?.let {
            Warning(
                "${it.label} adds roughly 150-300 ms of its own latency. The engine " +
                    "cannot compensate for that; wired or USB output is the only fix."
            )
        }

        // --- transport and the numbers that matter live -----------------------
        SectionCard("Status") {
            StatRow(
                "Round-trip latency",
                if (running) "%.1f ms".format(metrics.totalLatencyMs) else "--",
                emphasis = true
            )
            StatRow(
                "  engine",
                "%.1f ms".format(metrics.engineLatencyMs)
            )
            StatRow(
                "  hardware in + out",
                if (running) "%.1f ms".format(
                    (metrics.totalLatencyMs - metrics.engineLatencyMs).coerceAtLeast(0f)
                ) else "--"
            )
            StatRow("DSP load", "%.0f %%".format(metrics.cpuLoad * 100f))
            Meter(metrics.cpuLoad)
            StatRow("Dropouts since start", metrics.xRuns.toString())
            StatRow(
                "Stream",
                if (running) "${metrics.sampleRate} Hz / ${metrics.burstFrames} frames" else "stopped"
            )

            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Button(
                    onClick = { if (running) vm.stop() else vm.start() },
                    enabled = hasMicPermission
                ) {
                    Text(if (running) "Stop" else "Start")
                }
                OutlinedButton(onClick = { vm.panic() }) { Text("Panic") }
                OutlinedButton(onClick = { vm.refreshDevices() }) { Text("Rescan") }
            }
        }

        // --- the knob ---------------------------------------------------------
        SectionCard("Mix") {
            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                Knob(
                    value = settings.wetDry,
                    onValueChange = { v -> vm.update { it.copy(wetDry = v) } },
                    label = "WET / DRY",
                    valueText = "${(settings.wetDry * 100).toInt()} % wet",
                    startLabel = "dry",
                    endLabel = "wet"
                )
            }
            Text(
                "The dry path is delayed to match the engine exactly, so the two stay " +
                    "phase-aligned as the knob moves.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        // --- harmony ----------------------------------------------------------
        SectionCard("Harmony") {
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                HarmonyMode.entries.forEach { mode ->
                    FilterChip(
                        selected = settings.harmonyMode == mode,
                        onClick = { vm.update { it.copy(harmonyMode = mode) } },
                        label = { Text(mode.title) }
                    )
                }
            }
            Text(
                when (settings.harmonyMode) {
                    HarmonyMode.FIXED_INTERVAL ->
                        "Each MIDI note sets a fixed interval measured in cents from middle C. " +
                            "E above middle C is +400 cents, so that voice tracks 400 cents above " +
                            "whatever you play."
                    HarmonyMode.ABSOLUTE ->
                        "Each voice lands on the exact pitch of the note played, whatever you " +
                            "play. Needs a confident read on your pitch, so it works best on " +
                            "sustained, clearly voiced notes."
                    HarmonyMode.CHORD_VOICING ->
                        "Hold a chord and you supply one of its tones yourself; the rest is built " +
                            "around your pitch. The same shape anywhere on the keyboard gives the " +
                            "same chord, and no pitch tracking is needed, so this costs no more " +
                            "than a fixed interval."
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            if (settings.harmonyMode == HarmonyMode.ABSOLUTE) {
                StatRow(
                    "Detected pitch",
                    if (metrics.detectedPitchHz > 0f) "%.1f Hz".format(metrics.detectedPitchHz)
                    else "unvoiced"
                )
                if (settings.fftSize < 2048) {
                    Text(
                        "Window is ${settings.fftSize}; pitch tracking below " +
                            "${"%.0f".format(metrics.sampleRate / (settings.fftSize / 2f))} Hz is " +
                            "unreliable at this size. Use 2048 in Settings for low voices.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.tertiary
                    )
                }
            }

            if (settings.harmonyMode == HarmonyMode.CHORD_VOICING) {
                Text(
                    "You are playing the",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(
                    Modifier.horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ChordDegree.entries.forEach { degree ->
                        FilterChip(
                            selected = settings.chordDegree == degree,
                            onClick = { vm.update { it.copy(chordDegree = degree) } },
                            label = { Text(degree.title) }
                        )
                    }
                }
                Text(
                    settings.chordDegree.detail +
                        " If the chord you hold has no ${settings.chordDegree.title.lowercase()}, " +
                        "the root is used instead.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )

                ChordReadout(
                    heldNotes = heldNotes,
                    rootNote = metrics.rootNote,
                    anchorNote = metrics.anchorNote,
                    degree = settings.chordDegree
                )

                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.fillMaxWidth(0.78f)) {
                        Text("Double your own note", style = MaterialTheme.typography.bodyMedium)
                        Text(
                            "Off by default: you are already playing that tone, so the engine " +
                                "leaves it to you. Turn it on when running fully wet.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(
                        checked = settings.doubleAnchor,
                        onCheckedChange = { v -> vm.update { it.copy(doubleAnchor = v) } }
                    )
                }
            }

            StatRow("Voices sounding", "${metrics.activeVoices} / 10")
            VoiceDots(metrics.activeVoices)
            if (settings.harmonyMode == HarmonyMode.CHORD_VOICING && !settings.doubleAnchor &&
                heldNotes.isNotEmpty()
            ) {
                Text(
                    "One fewer than the notes you are holding: your own tone is not synthesised.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }

        // --- routing ----------------------------------------------------------
        SectionCard("Routing") {
            Picker(
                label = "Audio input",
                selectedLabel = inputs.firstOrNull { it.id == settings.inputDeviceId }?.label
                    ?: "Automatic (system default)",
                items = inputs,
                itemLabel = AudioDeviceOption::label,
                onSelect = { d -> vm.update { it.copy(inputDeviceId = d.id) } }
            )
            Picker(
                label = "Audio output",
                selectedLabel = outputs.firstOrNull { it.id == settings.outputDeviceId }?.label
                    ?: "Automatic (system default)",
                items = outputs,
                itemLabel = AudioDeviceOption::label,
                onSelect = { d -> vm.update { it.copy(outputDeviceId = d.id) } }
            )
            Picker(
                label = "MIDI controller",
                selectedLabel = midiConnected ?: "Not connected",
                items = midiPorts,
                itemLabel = { p: MidiPort -> if (p.isUsb) "${p.name} (USB)" else p.name },
                onSelect = { p -> vm.midi.open(p.id) }
            )
            if (midiConnected != null) {
                StatRow("Last note", lastNote ?: "--")
            } else {
                Text(
                    "Plug a controller into the USB-C port and tap Rescan. Bluetooth LE " +
                        "controllers show up here once the system has paired them.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }

        // --- levels -----------------------------------------------------------
        SectionCard("Levels") {
            StatRow("Input", "%.2f".format(metrics.inputPeak))
            Meter(metrics.inputPeak, warnAbove = 0.8f, dangerAbove = 0.98f)
            StatRow("Output", "%.2f".format(metrics.outputPeak))
            Meter(metrics.outputPeak, warnAbove = 0.8f, dangerAbove = 0.98f)
        }

        Text(
            "Quality mode: ${settings.qualityMode.title}  ·  ${qualityReadout(settings, metrics)}",
            style = MaterialTheme.typography.bodySmall,
            fontFamily = FontFamily.Monospace,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}


/**
 * Shows the chord being held, which tone the player is standing in for, and --
 * importantly -- when the requested degree was missing and the engine fell back
 * to the root, so that is never a silent surprise.
 */
@Composable
private fun ChordReadout(
    heldNotes: List<Int>,
    rootNote: Int,
    anchorNote: Int,
    degree: ChordDegree
) {
    if (heldNotes.isEmpty()) {
        Text(
            "Hold a chord to hear it.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        return
    }

    StatRow("Chord held", heldNotes.joinToString(" ") { MidiController.noteName(it) })

    if (heldNotes.size < 2) {
        Text(
            "That is only your own note. Hold at least two so there is something to " +
                "place around it.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.tertiary
        )
        return
    }

    if (rootNote < 0 || anchorNote < 0) return

    val fellBack = degree != ChordDegree.ROOT && anchorNote == rootNote
    if (fellBack) {
        Text(
            "No ${degree.title.lowercase()} in this chord, so you are the root " +
                "(${MidiController.noteName(rootNote)}).",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.tertiary
        )
    } else {
        StatRow(
            "You are the ${degree.title.lowercase()}",
            MidiController.noteName(anchorNote),
            emphasis = true
        )
        val intervals = heldNotes
            .filter { it != anchorNote }
            .joinToString(", ") { n ->
                val cents = (n - anchorNote) * 100
                if (cents >= 0) "+$cents" else "$cents"
            }
        StatRow("Voices at", "$intervals cents")
    }
}

internal fun qualityReadout(
    settings: com.dylan.harmonizer.HarmonizerSettings,
    metrics: com.dylan.harmonizer.EngineMetrics
): String = when (settings.qualityMode) {
    com.dylan.harmonizer.QualityMode.VOCODER -> "${metrics.partialsPerVoice} partials/voice"
    com.dylan.harmonizer.QualityMode.SAMPLE_RATE -> "%.0f Hz internal".format(metrics.internalSampleRate)
    com.dylan.harmonizer.QualityMode.BIT_DEPTH -> "${metrics.bitDepth}-bit wet path"
}
