package com.dylan.harmonizer

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.ContextCompat
import com.dylan.harmonizer.ui.HarmonizerApp
import com.dylan.harmonizer.ui.HarmonizerTheme

class MainActivity : ComponentActivity() {

    private val vm: HarmonizerViewModel by viewModels()

    private var hasMicPermission by mutableStateOf(false)

    private val requestPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            hasMicPermission = granted
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // A performer is not going to tap the screen every 30 seconds to stop it
        // sleeping mid-song.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        hasMicPermission = ContextCompat.checkSelfPermission(
            this, Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED

        setContent {
            HarmonizerTheme {
                HarmonizerApp(
                    vm = vm,
                    hasMicPermission = hasMicPermission,
                    onRequestPermission = {
                        requestPermission.launch(Manifest.permission.RECORD_AUDIO)
                    }
                )
            }
        }
    }

    override fun onStart() {
        super.onStart()
        // Devices may have been plugged in or unplugged while we were away.
        vm.refreshDevices()
    }

    override fun onStop() {
        super.onStop()
        // The engine holds the mic and an exclusive-mode low-latency output
        // stream. There is no foreground service here, so hand both back rather
        // than sitting on them while another app might want them.
        vm.stop()
    }
}
