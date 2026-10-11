/* Descent 1 & 2 VR, Android: the engine's warning and error boxes go to the
 * system log (adb logcat -s DescentVR); there is no desktop to show them on. */
#include <android/log.h>
#include "messagebox.h"

void msgbox_warning(char *message)
{
	__android_log_print(ANDROID_LOG_WARN, "DescentVR", "%s", message);
}

void msgbox_error(const char *message)
{
	__android_log_print(ANDROID_LOG_ERROR, "DescentVR", "%s", message);
}
