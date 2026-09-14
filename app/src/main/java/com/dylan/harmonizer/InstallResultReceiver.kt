package com.dylan.harmonizer

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import androidx.core.content.IntentCompat
import kotlinx.coroutines.flow.MutableStateFlow

/**
 * Receives the outcome of a package-installer session.
 *
 * The important case is STATUS_PENDING_USER_ACTION: Android will not install
 * anything without the user confirming, and it hands back the intent that shows
 * that prompt. Failing to launch it looks exactly like the update silently doing
 * nothing.
 */
class InstallResultReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        when (val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, -1)) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                val confirm = IntentCompat.getParcelableExtra(
                    intent, Intent.EXTRA_INTENT, Intent::class.java
                )
                if (confirm != null) {
                    confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    context.startActivity(confirm)
                } else {
                    InstallStatus.message.value = "Android did not return a confirmation prompt."
                }
            }

            PackageInstaller.STATUS_SUCCESS -> {
                // The process is about to be replaced; nothing useful to do.
                InstallStatus.message.value = null
            }

            else -> {
                val detail = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)
                InstallStatus.message.value = when {
                    detail?.contains("INCOMPATIBLE", ignoreCase = true) == true ||
                        detail?.contains("signature", ignoreCase = true) == true ->
                        "This build is signed with a different key than the one installed. " +
                            "Uninstall the app once, then install the new version."
                    status == PackageInstaller.STATUS_FAILURE_ABORTED ->
                        "Install cancelled."
                    detail != null -> "Install failed: $detail"
                    else -> "Install failed."
                }
            }
        }
    }

    companion object {
        const val ACTION_INSTALL_RESULT = "com.dylan.harmonizer.INSTALL_RESULT"
    }
}

/** Carries installer outcomes back to whatever is on screen. */
object InstallStatus {
    val message = MutableStateFlow<String?>(null)
}
