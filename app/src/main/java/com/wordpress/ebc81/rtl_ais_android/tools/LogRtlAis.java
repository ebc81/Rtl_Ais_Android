/*
 * rtl_ais_android is an Android port of the famous rtl_tcp rtl_fm rtl_ais driver for
 * RTL2832U based USB DVB-T dongles. It does not require root.
 * Base on project rtl_tcp_andro by
 * Copyright (C) 2012-2014 by Martin Marinov <martintzvetomirov@gmail.com>
 *
 * Port version by
 * Copyright (C) 2015 by Christian Ebner <cebner@gmx.at> ebc81.wordpress.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
package com.wordpress.ebc81.rtl_ais_android.tools;

import java.util.ArrayList;

public abstract class LogRtlAis {
    private final static StringBuilder log = new StringBuilder();
    private final static Object locker = new Object();
    private final static ArrayList<Callback> callbacks = new ArrayList<LogRtlAis.Callback>();


    public static void appendLine(String what) {
        while (what.charAt(what.length()-1) == '\n') {
            what = what.substring(0, what.length()-1);
        }
        what+="\n";
        synchronized (locker) {
            log.append(what);
            for (final LogRtlAis.Callback callback : callbacks) callback.onAppend(what);
        }

    }

    public static String getFullLog() {
        return log.toString();
    }

    public static void clear() {
        synchronized (locker) {
            log.setLength(0);
            log.trimToSize();
            for (final LogRtlAis.Callback callback : callbacks) callback.onClear();
        }
    }

    public static void registerCallback(final LogRtlAis.Callback callback) {
        synchronized (locker) {
            if (!callbacks.contains(callback)) callbacks.add(callback);
        }
    }

    public static void unregisterCallback(final LogRtlAis.Callback callback) {
        synchronized (locker) {
            callbacks.remove(callback);
        }
    }

    public static void announceStateChanged(final boolean state) {
        synchronized (locker) {
            for (final LogRtlAis.Callback callback : callbacks) callback.onServiceStatusChanged(state);
        }
    }

    public  static interface Callback {
        public void onClear();
        public void onAppend(final String str);
        public void onServiceStatusChanged(boolean on);
    }
}