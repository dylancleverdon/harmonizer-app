package com.dylan.harmonizer.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlin.math.cos
import kotlin.math.sin

/**
 * Rotary control driven by vertical drag. Drag distance rather than angle, so
 * the knob does not jump when a finger lands off-centre -- which is what
 * happens when someone reaches for it mid-performance.
 */
@Composable
fun Knob(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    size: Dp = 168.dp,
    label: String = "",
    valueText: String = "",
    startLabel: String = "",
    endLabel: String = ""
) {
    val current = rememberUpdatedState(value)
    val trackColor = MaterialTheme.colorScheme.surfaceVariant
    val accent = MaterialTheme.colorScheme.primary
    val dialColor = MaterialTheme.colorScheme.surface
    val outline = MaterialTheme.colorScheme.onSurfaceVariant

    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = modifier) {
        if (label.isNotEmpty()) {
            Text(label, style = MaterialTheme.typography.labelLarge, color = outline)
        }
        Canvas(
            modifier = Modifier
                .size(size)
                .pointerInput(Unit) {
                    detectVerticalDragGestures { _, dragAmount ->
                        // A full sweep takes roughly 300 px of travel.
                        val next = (current.value - dragAmount / 300f).coerceIn(0f, 1f)
                        onValueChange(next)
                    }
                }
        ) {
            val stroke = 14.dp.toPx()
            val inset = stroke / 2f + 6.dp.toPx()
            val arcSize = Size(this.size.width - inset * 2, this.size.height - inset * 2)
            val topLeft = Offset(inset, inset)

            // 270 degrees of travel, the usual hardware convention.
            val startAngle = 135f
            val sweep = 270f

            drawArc(
                color = trackColor, startAngle = startAngle, sweepAngle = sweep,
                useCenter = false, topLeft = topLeft, size = arcSize,
                style = Stroke(width = stroke)
            )
            drawArc(
                color = accent, startAngle = startAngle, sweepAngle = sweep * current.value,
                useCenter = false, topLeft = topLeft, size = arcSize,
                style = Stroke(width = stroke)
            )

            val radius = arcSize.width / 2f
            val centre = Offset(this.size.width / 2f, this.size.height / 2f)
            drawCircle(color = dialColor, radius = radius - stroke, center = centre)

            val angleRad = Math.toRadians((startAngle + sweep * current.value).toDouble())
            val pointerOuter = Offset(
                centre.x + (radius - stroke * 1.2f) * cos(angleRad).toFloat(),
                centre.y + (radius - stroke * 1.2f) * sin(angleRad).toFloat()
            )
            val pointerInner = Offset(
                centre.x + (radius * 0.32f) * cos(angleRad).toFloat(),
                centre.y + (radius * 0.32f) * sin(angleRad).toFloat()
            )
            drawLine(
                color = accent, start = pointerInner, end = pointerOuter,
                strokeWidth = 5.dp.toPx()
            )
            drawCircle(color = Color(0x22FFFFFF), radius = radius * 0.1f, center = centre)
        }
        if (valueText.isNotEmpty()) {
            Text(valueText, style = MaterialTheme.typography.titleMedium)
        }
        if (startLabel.isNotEmpty() || endLabel.isNotEmpty()) {
            androidx.compose.foundation.layout.Row(
                horizontalArrangement = Arrangement.SpaceBetween,
                modifier = Modifier.size(width = size, height = 20.dp)
            ) {
                Text(startLabel, style = MaterialTheme.typography.labelSmall, color = outline)
                Text(endLabel, style = MaterialTheme.typography.labelSmall, color = outline)
            }
        }
    }
}
