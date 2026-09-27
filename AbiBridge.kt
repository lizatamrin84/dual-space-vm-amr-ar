package com.app.dualspace.bridge

import android.os.Build
import android.util.Log
import java.io.File

object AbiBridge {

    private const val TAG = "AbiBridge"

    // Supported Android Architectures
    const val ABI_ARM64_V8A = "arm64-v8a"
    const val ABI_ARMEABI_V7A = "armeabi-v7a"
    const val ABI_X86_64 = "x86_64"
    const val ABI_X86 = "x86"

    init {
        try {
            System.loadLibrary("native-bridge")
            Log.i(TAG, "native-bridge library loaded successfully.")
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Failed to load native-bridge library", e)
        }
    }

    /* ---------------- Native Methods ---------------- */

    external fun getNativeCompiledAbi(): String
    external fun isProcess64Bit(): Boolean
    private external fun nativeLoadLibrary(libPath: String): Long
    private external fun nativeFindSymbol(handle: Long, symbolName: String): Long
    private external fun nativeCloseLibrary(handle: Long): Boolean

    /* ---------------- Kotlin ABI Helpers ---------------- */

    /**
     * Returns the device's primary architecture.
     */
    val primaryAbi: String
        get() = Build.SUPPORTED_ABIS.firstOrNull() ?: "unknown"

    /**
     * Complete list of all ABIs supported by this device in order of preference.
     */
    val supportedAbis: List<String>
        get() = Build.SUPPORTED_ABIS.toList()

    /**
     * List of supported 64-bit architectures on this device.
     */
    val supported64BitAbis: List<String>
        get() = Build.SUPPORTED_64_BIT_ABIS.toList()

    /**
     * List of supported 32-bit architectures on this device.
     */
    val supported32BitAbis: List<String>
        get() = Build.SUPPORTED_32_BIT_ABIS.toList()

    /**
     * Determines whether the current process can load a shared library compiled for [targetAbi].
     */
    fun isAbiCompatible(targetAbi: String): Boolean {
        val currentProcessIs64 = isProcess64Bit()

        return when (targetAbi) {
            ABI_ARM64_V8A, ABI_X86_64 -> {
                currentProcessIs64 && supported64BitAbis.contains(targetAbi)
            }
            ABI_ARMEABI_V7A, ABI_X86 -> {
                !currentProcessIs64 && supported32BitAbis.contains(targetAbi)
            }
            else -> false
        }
    }

    /**
     * Matches the best compatible ABI from an available set of ABI folders extracted from an APK.
     *
     * @param availableAbis List of ABI directory names found inside `lib/` (e.g., ["armeabi-v7a", "arm64-v8a"]).
     * @return Best matching ABI string, or null if no compatible ABI is supported by this runtime.
     */
    fun selectBestAbi(availableAbis: Collection<String>): String? {
        val processIs64 = isProcess64Bit()
        
        // Filter candidate ABIs based on the 32-bit vs 64-bit state of the current host process
        val processPool = if (processIs64) supported64BitAbis else supported32BitAbis

        for (supported in processPool) {
            if (availableAbis.contains(supported)) {
                return supported
            }
        }
        return null
    }

    /**
     * Safely loads an extracted .so dynamic library from the sandbox filesystem.
     *
     * @param soFile The File object pointing to the library on storage.
     * @return [LoadedLibrary] wrapper, or null if loading failed.
     */
    fun loadLibrary(soFile: File): LoadedLibrary? {
        if (!soFile.exists()) {
            Log.e(TAG, "loadLibrary failed: File does not exist at ${soFile.absolutePath}")
            return null
        }

        if (!soFile.canRead()) {
            Log.e(TAG, "loadLibrary failed: No read permissions for ${soFile.absolutePath}")
            return null
        }

        val handle = nativeLoadLibrary(soFile.absolutePath)
        return if (handle != 0L) {
            LoadedLibrary(handle, soFile.name, soFile.absolutePath)
        } else {
            Log.e(TAG, "nativeLoadLibrary returned handle 0 for ${soFile.name}")
            null
        }
    }

    /**
     * Safe container representing an open .so handle. Implements [AutoCloseable]
     * so it can be used cleanly with `.use { ... }`.
     */
    class LoadedLibrary internal constructor(
        val handle: Long,
        val libraryName: String,
        val absolutePath: String
    ) : AutoCloseable {

        private var isClosed = false

        /**
         * Resolves an exported symbol address from this library.
         */
        fun findSymbol(symbolName: String): Long {
            check(!isClosed) { "Cannot find symbol on closed library handle: $libraryName" }
            return nativeFindSymbol(handle, symbolName)
        }

        /**
         * Closes the library handle via dlclose.
         */
        override fun close() {
            if (!isClosed) {
                val closed = nativeCloseLibrary(handle)
                if (closed) {
                    isClosed = true
                } else {
                    Log.w(TAG, "Failed to cleanly unload library: $libraryName")
                }
            }
        }
    }
}