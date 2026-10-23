// Descent 1 & 2 VR on Quest and Steam Frame (standalone): a helper for the PC's
// Setup.  Setup copies the Game Tome's book to the headset over adb, and a
// folder adb makes is adb's: the game may read it but not write the player's
// notes and the ribbon's place into it.  So Setup asks the app, with an
// explicit broadcast, to make the book's folders itself, and copies the book
// into them.  The app also proves it can write in each one (a file made and
// removed), and answers 1 only then.  No window, nothing else.
package net.gameordie.descentvr;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;

public class SetupReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        int made = 0, writable = 0;
        for (boolean descent2 : new boolean[] { false, true }) {
            File base = DescentActivity.dataDir(context, descent2);
            for (String sub : new String[] { "tome", "tome/pages", "tome/sounds" }) {
                File d = new File(base, sub);
                if (d.isDirectory() || d.mkdirs())
                    made++;
            }
            // Notes Setup put back after a reinstall (name.restore, copied over adb):
            // taken in as the app's own files, which the game can then rewrite.
            for (String name : new String[] { "ink.txt", "place.txt" }) {
                File from = new File(base, "tome/" + name + ".restore"), to = new File(base, "tome/" + name);
                if (!from.isFile())
                    continue;
                if (!to.exists()) {
                    File tmp = new File(base, "tome/" + name + ".new");
                    try (java.io.InputStream in = new java.io.FileInputStream(from);
                         FileOutputStream out = new FileOutputStream(tmp)) {
                        byte[] buf = new byte[8192];
                        int n;
                        while ((n = in.read(buf)) > 0)
                            out.write(buf, 0, n);
                    } catch (Exception e) {
                        Log.w("DescentVR", "Setup: could not take in " + from + ": " + e);
                        continue;
                    }
                    if (!tmp.renameTo(to))
                        Log.w("DescentVR", "Setup: could not take in " + from);
                }
                from.delete();
            }
            File probe = new File(base, "tome/.setup-write-test");
            try {
                new FileOutputStream(probe).close();
                if (probe.delete())
                    writable++;
            } catch (Exception e) {
                Log.w("DescentVR", "Setup: cannot write in " + probe.getParent() + ": " + e);
            }
        }
        Log.i("DescentVR", "Setup: the Game Tome's folders: " + made + " of 6 made, " + writable + " of 2 writable");
        setResultCode(made == 6 && writable == 2 ? 1 : 0);
        setResultData(made + " made, " + writable + " writable");
    }
}
