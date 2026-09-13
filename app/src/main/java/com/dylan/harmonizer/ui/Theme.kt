package com.dylan.harmonizer.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val Accent = Color(0xFF4DD0C0)
private val AccentDim = Color(0xFF2A7D74)
private val Warn = Color(0xFFE8A33D)

private val DarkColors = darkColorScheme(
    primary = Accent,
    onPrimary = Color(0xFF00201C),
    secondary = AccentDim,
    tertiary = Warn,
    background = Color(0xFF0E1113),
    onBackground = Color(0xFFE3E6E8),
    surface = Color(0xFF171B1E),
    onSurface = Color(0xFFE3E6E8),
    surfaceVariant = Color(0xFF232A2E),
    onSurfaceVariant = Color(0xFFA8B4B8),
    error = Color(0xFFE05C5C)
)

private val LightColors = lightColorScheme(
    primary = AccentDim,
    secondary = Accent,
    tertiary = Warn
)

@Composable
fun HarmonizerTheme(content: @Composable () -> Unit) {
    // The app is meant to be used on a dark stage; dark is the default and the
    // light scheme is only there for people who force it system-wide.
    MaterialTheme(
        colorScheme = if (isSystemInDarkTheme()) DarkColors else LightColors,
        content = content
    )
}
