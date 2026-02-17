#!/system/bin/sh

if [ -n "$KSU" ]; then
	ui_print "* KernelSU detected. Make sure you are using a Zygisk module!"
fi

ui_print "* Do not put the apps for which you want to take an ss"
ui_print "* in any kind of denylist and disable 'Unmount modules'"
ui_print "* option for them"


ui_print ""
ui_print "  by j-hc (github.com/j-hc)"
