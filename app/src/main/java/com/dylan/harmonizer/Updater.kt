package com.dylan.harmonizer

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.pm.PackageInfoCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/**
 * In-app updater.
 *
 * Checks a small JSON manifest published beside the APK on the project's
 * releases page, and if it advertises a higher versionCode, downloads that APK
 * and hands it to Android's package installer.
 *
 * This only works because every build is signed with the same key. Android
 * refuses to install over an app signed by a different key, so an updater on top
 * of per-build throwaway keys would fail at the final step every time.
 */
class Updater(private val context: Context) {

    sealed interface State {
        data object Idle : State
        data object Checking : State
        data class UpToDate(val versionName: String) : State
        data class Available(val info: ReleaseInfo) : State
        data class Downloading(val info: ReleaseInfo, val progress: Float) : State
        data class ReadyToInstall(val info: ReleaseInfo) : State
        data class Failed(val message: String) : State
    }

    data class ReleaseInfo(
        val versionCode: Long,
        val versionName: String,
        val notes: String,
        val sizeBytes: Long,
        val commit: String
    )

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state

    /** versionCode of the build currently installed. */
    val installedVersionCode: Long
        get() = runCatching {
            PackageInfoCompat.getLongVersionCode(
                context.packageManager.getPackageInfo(context.packageName, 0)
            )
        }.getOrDefault(0L)

    val installedVersionName: String
        get() = runCatching {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName
        }.getOrNull() ?: "unknown"

    /**
     * Android requires explicit per-app consent to install packages. Without it
     * the installer session is created and then silently refused, so it is worth
     * checking before downloading 19 MB.
     */
    fun canInstall(): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.packageManager.canRequestPackageInstalls()
        } else {
            true
        }

    fun permissionIntent(): Intent =
        Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES)
            .setData(Uri.parse("package:${context.packageName}"))

    suspend fun check() {
        _state.value = State.Checking
        val result = withContext(Dispatchers.IO) {
            runCatching { fetchManifest() }
        }
        result.fold(
            onSuccess = { info ->
                _state.value = if (info.versionCode > installedVersionCode) {
                    State.Available(info)
                } else {
                    State.UpToDate(installedVersionName)
                }
            },
            onFailure = { e ->
                _state.value = State.Failed(
                    e.message ?: "Could not reach the releases page."
                )
            }
        )
    }

    private fun fetchManifest(): ReleaseInfo {
        val text = openUrl(BuildConfig.UPDATE_MANIFEST_URL).use { it.readBytes().decodeToString() }
        val o = JSONObject(text)
        return ReleaseInfo(
            versionCode = o.optLong("versionCode", 0L),
            versionName = o.optString("versionName", "unknown"),
            notes = o.optString("notes", ""),
            sizeBytes = o.optLong("sizeBytes", 0L),
            commit = o.optString("commit", "")
        )
    }

    /** Downloads the APK to the cache directory, reporting progress as it goes. */
    suspend fun download(info: ReleaseInfo): File? = withContext(Dispatchers.IO) {
        val target = File(context.cacheDir, "update.apk")
        runCatching {
            target.delete()
            openUrl(BuildConfig.UPDATE_APK_URL).use { input ->
                target.outputStream().use { output ->
                    val buffer = ByteArray(64 * 1024)
                    var total = 0L
                    while (true) {
                        val n = input.read(buffer)
                        if (n <= 0) break
                        output.write(buffer, 0, n)
                        total += n
                        if (info.sizeBytes > 0) {
                            _state.value = State.Downloading(
                                info, (total.toFloat() / info.sizeBytes).coerceIn(0f, 1f)
                            )
                        }
                    }
                }
            }
            target
        }.getOrElse { e ->
            target.delete()
            _state.value = State.Failed(e.message ?: "Download failed.")
            null
        }
    }

    /**
     * Streams the downloaded APK into a package-installer session and commits
     * it. Android then shows its own confirmation, and replaces this process on
     * success -- so there is nothing to do afterwards.
     */
    suspend fun install(apk: File, info: ReleaseInfo) = withContext(Dispatchers.IO) {
        runCatching {
            val installer = context.packageManager.packageInstaller
            val params = PackageInstaller.SessionParams(
                PackageInstaller.SessionParams.MODE_FULL_INSTALL
            )
            params.setAppPackageName(context.packageName)

            val sessionId = installer.createSession(params)
            installer.openSession(sessionId).use { session ->
                session.openWrite("harmonizer", 0, apk.length()).use { out ->
                    apk.inputStream().use { it.copyTo(out) }
                    session.fsync(out)
                }

                val intent = Intent(context, InstallResultReceiver::class.java)
                    .setAction(InstallResultReceiver.ACTION_INSTALL_RESULT)
                // Mutable because the system fills in the status extras.
                val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
                val pending = PendingIntent.getBroadcast(context, sessionId, intent, flags)
                session.commit(pending.intentSender)
            }
            _state.value = State.ReadyToInstall(info)
        }.onFailure { e ->
            _state.value = State.Failed(e.message ?: "Install could not be started.")
        }
        Unit
    }

    fun reset() {
        _state.value = State.Idle
    }

    fun fail(message: String) {
        _state.value = State.Failed(message)
    }

    private fun openUrl(url: String): java.io.InputStream {
        var current = url
        // GitHub's "latest release" links redirect, and HttpURLConnection will
        // not follow a redirect that switches protocol, so follow them by hand.
        repeat(5) {
            val conn = (URL(current).openConnection() as HttpURLConnection).apply {
                connectTimeout = 15_000
                readTimeout = 30_000
                instanceFollowRedirects = false
                setRequestProperty("Accept", "*/*")
                setRequestProperty("User-Agent", "Harmonizer-Updater")
            }
            when (val code = conn.responseCode) {
                in 200..299 -> return conn.inputStream
                301, 302, 303, 307, 308 -> {
                    val location = conn.getHeaderField("Location")
                    conn.disconnect()
                    if (location.isNullOrBlank()) error("Redirect with no destination.")
                    current = URL(URL(current), location).toString()
                }
                404 -> {
                    conn.disconnect()
                    error("No release published yet.")
                }
                else -> {
                    conn.disconnect()
                    error("Server returned $code.")
                }
            }
        }
        error("Too many redirects.")
    }
}
