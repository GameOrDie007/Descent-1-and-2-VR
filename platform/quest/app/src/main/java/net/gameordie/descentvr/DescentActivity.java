// Descent 1 & 2 VR on Quest and Steam Frame (standalone): a game, in the headset.
// SDL2's activity runs the engine, which is written against SDL 1.2 and gets
// it from sdl12-compat on top of SDL2.  This class is Descent; Descent2Activity
// is Descent II.  Each runs in its own process, so SDL starts fresh every time.
package net.gameordie.descentvr;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.system.Os;
import android.util.Log;
import java.io.File;
import org.libsdl.app.SDLActivity;

public class DescentActivity extends SDLActivity {
    // Where each game's files go, under Android/data/<package>/files.
    // Descent keeps the top folder (the first builds used it); Descent II has its own.
    static File dataDir(Context c, boolean descent2) {
        File d = c.getExternalFilesDir(descent2 ? "descent2" : null);
        return d != null ? d : c.getFilesDir();
    }

    protected boolean isDescent2() {
        return false;
    }

    private String dataDir() {
        return dataDir(this, isDescent2()).getAbsolutePath();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            // Settings and pilots live beside the game data (PhysFS takes HOME).
            Os.setenv("HOME", dataDir(), true);
            // The engine draws with OpenGL ES 1; sdl12-compat's scaling is desktop GL.
            Os.setenv("SDL12COMPAT_OPENGL_SCALING", "0", true);
        } catch (Exception e) {
            Log.w("DescentVR", "setenv: " + e);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "SDL", isDescent2() ? "main_d2" : "main_d1" };
    }

    @Override
    protected String getMainFunction() {
        return "main";	// the engine's own main(), not SDL_main
    }

    // Switch Game and quitting (vrxr_start_program): one of the app's screens.
    public void startScreen(String which) {
        Class<?> c = "descent2".equals(which) ? Descent2Activity.class
                : "descent".equals(which) ? DescentActivity.class : PickerActivity.class;
        Intent i = new Intent(this, c);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(i);
    }

    // The game has ended (its main returned and SDL finished the activity): end
    // the process too.  Android keeps it otherwise, with the music's audio
    // stream still playing, and the headset shows nothing but black.
    @Override
    protected void onDestroy() {
        super.onDestroy();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override
    protected String[] getArguments() {
        return new String[] { "-vr", "-hogdir", dataDir() };	// -vr: the headset, as the PC launchers ask
    }
}
