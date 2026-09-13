package com.dylan.harmonizer.ui

import androidx.compose.foundation.clickable
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
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.dylan.harmonizer.HarmonizerViewModel
import com.dylan.harmonizer.NativeBridge
import com.dylan.harmonizer.QualityMode

@Composable
fun SettingsScreen(vm: HarmonizerViewModel, onBack: () -> Unit) {
    val settings by vm.settings.collectAsState()
    val metrics by vm.metrics.collectAsState()

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
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(512, 1024, 2048).forEach { size ->
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
                    "512 gives ${engineLatency(512)} ms but resolves low notes poorly; " +
                    "2048 gives ${engineLatency(2048)} ms and is the right choice for " +
                    "absolute-pitch mode on low voices.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            StatRow("Engine latency at this size", "${engineLatency(settings.fftSize)} ms", emphasis = true)
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

/** Mirrors the engine's own latency formula: (3/4 * window + resampler) / rate. */
private fun engineLatency(fftSize: Int): String =
    "%.1f".format((fftSize * 0.75f + 63f) / 48000f * 1000f)
