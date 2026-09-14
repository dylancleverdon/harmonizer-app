package com.dylan.harmonizer.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.dylan.harmonizer.HarmonizerViewModel
import com.dylan.harmonizer.NativeBridge
import com.dylan.harmonizer.InstallStatus
import com.dylan.harmonizer.QualityMode
import com.dylan.harmonizer.Updater
import com.dylan.harmonizer.StreamRate

@Composable
fun SettingsScreen(vm: HarmonizerViewModel, onBack: () -> Unit) {
    val settings by vm.settings.collectAsState()
    val metrics by vm.metrics.collectAsState()
    // Fall back to 48 kHz for the previews before a stream has ever been opened.
    val rate = if (metrics.sampleRate > 0) metrics.sampleRate else 48000

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
            Text("Settings", style = MaterialTheme.typography.headlineSmall)
            TextButton(onClick = onBack) { Text("Done") }
        }

        // --- updates ----------------------------------------------------------
        UpdatesCard(vm)

        // --- the three quality-reduction strategies ---------------------------
        SectionCard("Quality reduction method") {
            Text(
                "How the wet path gives up quality to save processing time. " +
                    "One method at a time; the slider below sets how far it goes.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            QualityMode.entries.forEach { mode ->
                QualityOption(
                    mode = mode,
                    selected = settings.qualityMode == mode,
                    onSelect = { vm.update { s -> s.copy(qualityMode = mode) } }
                )
            }
        }

        SectionCard("Reduction amount") {
            Slider(
                value = settings.qualityAmount,
                onValueChange = { v -> vm.update { it.copy(qualityAmount = v) } },
                valueRange = 0f..1f
            )
            StatRow("Setting", "${(settings.qualityAmount * 100).toInt()} %")
            StatRow("Currently running", qualityReadout(settings, metrics), emphasis = true)
            if (metrics.effectiveQuality > settings.qualityAmount + 0.02f) {
                Text(
                    "An adaptive option is currently pushing this to " +
                        "${(metrics.effectiveQuality * 100).toInt()} %.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.tertiary
                )
            }
        }

        // --- the two experimental controllers ---------------------------------
        SectionCard("Experimental") {
            ToggleRow(
                title = "Adapt to measured latency",
                detail = "Watches how close the DSP is running to the callback deadline and " +
                    "quietly increases the reduction amount before a dropout happens, then " +
                    "backs off again when there is headroom.",
                checked = settings.adaptiveLatency,
                onChange = { v -> vm.update { it.copy(adaptiveLatency = v) } }
            )
            ToggleRow(
                title = "Reduce quality as voices are added",
                detail = "Scales the reduction amount up with the number of notes held, so a " +
                    "ten-note chord costs closer to what one note costs.",
                checked = settings.adaptiveVoiceScaling,
                onChange = { v -> vm.update { it.copy(adaptiveVoiceScaling = v) } }
            )
            StatRow("Effective amount now", "${(metrics.effectiveQuality * 100).toInt()} %")
        }

        // --- the audio stream itself, as opposed to the wet path ------------
        SectionCard("Audio stream") {
            Text(
                "This is the rate everything runs at, not just the harmonies. Lowering it " +
                    "means every stage handles fewer samples per second while the callback " +
                    "deadline stays the same length, which is the most direct way to stop " +
                    "dropouts. The cost is bandwidth, and a longer window in milliseconds " +
                    "for the same window size.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                StreamRate.entries.forEach { option ->
                    FilterChip(
                        selected = settings.streamRate == option,
                        onClick = { vm.update { it.copy(streamRate = option) } },
                        label = { Text(option.title) }
                    )
                }
            }
            StatRow(
                "Running at",
                if (metrics.sampleRate > 0) "${metrics.sampleRate} Hz" else "--",
                emphasis = true
            )
            if (settings.streamRate != StreamRate.DEVICE &&
                metrics.sampleRate > 0 && metrics.sampleRate != settings.streamRate.hz
            ) {
                Text(
                    "The device would not open at ${settings.streamRate.hz} Hz, so it is " +
                        "running at ${metrics.sampleRate} Hz instead.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.tertiary
                )
            }
            Text(
                "Device default is the cheapest of all, because nothing has to be converted.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            Text(
                "Output buffer",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(1, 2, 3, 4).forEach { n ->
                    FilterChip(
                        selected = settings.bufferBursts == n,
                        onClick = { vm.update { it.copy(bufferBursts = n) } },
                        label = { Text(if (n == 1) "1 burst" else "$n bursts") }
                    )
                }
            }
            Text(
                "How much slack the output has before a late callback becomes an audible " +
                    "gap. 1 is the tightest and the least forgiving; raise it if you hear " +
                    "crackle under a big chord. Each burst costs about " +
                    (if (metrics.burstFrames > 0 && metrics.sampleRate > 0)
                        "%.1f ms".format(metrics.burstFrames * 1000f / metrics.sampleRate)
                     else "one burst") + " of latency.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            StatRow(
                "Buffer / burst",
                if (metrics.burstFrames > 0)
                    "${metrics.bufferFrames} / ${metrics.burstFrames} frames"
                else "--"
            )
            StatRow("Dropouts since start", metrics.xRuns.toString())
            if (metrics.xRuns > 0) {
                Text(
                    "Dropouts are being counted. Raise the buffer, lower the stream rate, or " +
                        "push the reduction amount up until this stops climbing.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.tertiary
                )
            }
            Text(
                "Changing either of these reopens the audio streams, so there is a brief gap.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        // --- engine -----------------------------------------------------------
        SectionCard("Engine") {
            ToggleRow(
                title = "Formant correction",
                detail = "Keeps the vowel where it was while the pitch moves. Turning this off " +
                    "is cheaper but shifted voices sound chipmunk-like going up and ogre-like " +
                    "going down.",
                checked = settings.formantCorrection,
                onChange = { v -> vm.update { it.copy(formantCorrection = v) } }
            )

            Text(
                "Analysis window",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Row(
                Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                listOf(256, 512, 1024, 2048).forEach { size ->
                    FilterChip(
                        selected = settings.fftSize == size,
                        onClick = { vm.update { it.copy(fftSize = size) } },
                        label = { Text("$size") }
                    )
                }
            }
            Text(
                "This is the single biggest lever on latency, and it is the one thing the " +
                    "quality modes deliberately do not touch. " +
                    "256 gives ${engineLatency(256, rate)} ms but resolves low notes poorly; " +
                    "2048 gives ${engineLatency(2048, rate)} ms and is the right choice for " +
                    "absolute-pitch mode on low voices.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            StatRow(
                "Engine latency at this size",
                "${engineLatency(settings.fftSize, rate)} ms",
                emphasis = true
            )
            if (rate < 44100) {
                Text(
                    "At $rate Hz a window of ${settings.fftSize} samples lasts longer in " +
                        "milliseconds than it would at 48 kHz. Drop the window size a step to " +
                        "win that time back.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.tertiary
                )
            }
        }

        SectionCard("Microphone") {
            Text(
                "Android applies processing to mic input unless asked not to. Unprocessed is " +
                    "right for singing; the others re-enable gain control and noise suppression, " +
                    "which fight the harmoniser.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            val presets = listOf(
                NativeBridge.InputPreset.UNPROCESSED to "Unprocessed (recommended)",
                NativeBridge.InputPreset.VOICE_PERFORMANCE to "Voice performance",
                NativeBridge.InputPreset.CAMCORDER to "Camcorder",
                NativeBridge.InputPreset.GENERIC to "Generic",
                NativeBridge.InputPreset.VOICE_RECOGNITION to "Voice recognition"
            )
            Picker(
                label = "Input preset",
                selectedLabel = presets.firstOrNull { it.first == settings.inputPreset }?.second
                    ?: "Unprocessed (recommended)",
                items = presets,
                itemLabel = { it.second },
                onSelect = { p -> vm.update { it.copy(inputPreset = p.first) } }
            )
        }

        SectionCard("Output") {
            Text(
                "Output gain",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Slider(
                value = settings.outputGain,
                onValueChange = { v -> vm.update { it.copy(outputGain = v) } },
                valueRange = 0f..2f
            )
            StatRow("Gain", "%.2f x".format(settings.outputGain))
            ToggleRow(
                title = "Bypass",
                detail = "Passes the dry signal straight through, still delayed by the same " +
                    "amount, so switching in and out does not shift the timing.",
                checked = settings.bypass,
                onChange = { v -> vm.update { it.copy(bypass = v) } }
            )
        }
    }
}


/**
 * Checks the project's releases page for a newer build and installs it in place.
 *
 * This only works because every build is signed with the same committed key.
 * With the throwaway per-build keys this app shipped with originally, Android
 * would reject the install at the last step, so the failure message below calls
 * that case out by name rather than leaving it as a generic failure.
 */
@Composable
private fun UpdatesCard(vm: HarmonizerViewModel) {
    val state by vm.updater.state.collectAsState()
    val installMessage by InstallStatus.message.collectAsState()
    val context = LocalContext.current

    SectionCard("Updates") {
        StatRow(
            "Installed",
            "${vm.updater.installedVersionName} (${vm.updater.installedVersionCode})"
        )

        when (val s = state) {
            is Updater.State.Idle -> {
                Button(onClick = { vm.checkForUpdate() }) { Text("Check for updates") }
            }

            is Updater.State.Checking -> {
                Text("Checking...", style = MaterialTheme.typography.bodyMedium)
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }

            is Updater.State.UpToDate -> {
                Text(
                    "Up to date.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.primary
                )
                OutlinedButton(onClick = { vm.checkForUpdate() }) { Text("Check again") }
            }

            is Updater.State.Available -> {
                StatRow("Available", s.info.versionName, emphasis = true)
                if (s.info.notes.isNotBlank()) {
                    Text(
                        s.info.notes,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                if (s.info.sizeBytes > 0) {
                    StatRow("Download size", "%.1f MB".format(s.info.sizeBytes / 1024f / 1024f))
                }
                if (!vm.updater.canInstall()) {
                    Warning(
                        "Android needs permission to let this app install updates. Grant it " +
                            "once and it will not ask again."
                    )
                    Button(onClick = { context.startActivity(vm.updater.permissionIntent()) }) {
                        Text("Allow installs")
                    }
                } else {
                    Text(
                        "Audio stops while the update installs.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Button(onClick = { vm.downloadAndInstall(s.info) }) {
                        Text("Download and install")
                    }
                }
            }

            is Updater.State.Downloading -> {
                StatRow("Downloading", "${(s.progress * 100).toInt()} %")
                LinearProgressIndicator(
                    progress = { s.progress },
                    modifier = Modifier.fillMaxWidth()
                )
            }

            is Updater.State.ReadyToInstall -> {
                Text(
                    "Confirm the install when Android asks. The app will restart on the new " +
                        "version.",
                    style = MaterialTheme.typography.bodyMedium
                )
            }

            is Updater.State.Failed -> {
                Warning(s.message)
                OutlinedButton(onClick = { vm.updater.reset() }) { Text("Dismiss") }
            }
        }

        installMessage?.let { Warning(it) }
    }
}

@Composable
private fun QualityOption(mode: QualityMode, selected: Boolean, onSelect: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable { onSelect() }
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.Top
    ) {
        RadioButton(selected = selected, onClick = onSelect)
        Column(Modifier.padding(start = 4.dp, top = 10.dp)) {
            Text(mode.title, style = MaterialTheme.typography.bodyLarge)
            Text(
                qualityModeDetail(mode),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

private fun qualityModeDetail(mode: QualityMode): String = when (mode) {
    QualityMode.VOCODER ->
        "Resynthesises each voice from fewer spectral peaks, the way a vocoder reduces a " +
            "signal to a handful of bands. 96 partials down to 6. Cost falls directly with " +
            "the count and voice degrades gracefully, which is why this is the default."
    QualityMode.SAMPLE_RATE ->
        "Runs the wet path at 48, 24 or 12 kHz. The analysis window shrinks with the rate, " +
            "so the transform gets cheaper while the window duration -- and the latency -- " +
            "stays exactly where it was."
    QualityMode.BIT_DEPTH ->
        "Quantises the wet path from 24 bits down to 4. Worth knowing: this one buys tone, " +
            "not speed. Everything downstream is 32-bit float on this chip, so quantising " +
            "adds a step rather than removing work. Measured cost is within noise of 24-bit."
}

@Composable
private fun ToggleRow(
    title: String,
    detail: String,
    checked: Boolean,
    onChange: (Boolean) -> Unit
) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Top) {
        Column(Modifier.fillMaxWidth(0.78f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            Text(
                detail,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Switch(checked = checked, onCheckedChange = onChange, modifier = Modifier.padding(start = 8.dp))
    }
}

/**
 * Mirrors the engine's own latency formula: (3/4 * window + resampler) / rate.
 * The rate is a parameter rather than a constant because the stream rate is now
 * adjustable, and quoting 48 kHz figures at 24 kHz would be off by double.
 */
private fun engineLatency(fftSize: Int, sampleRate: Int): String =
    "%.1f".format((fftSize * 0.75f + 63f) / sampleRate.toFloat() * 1000f)
