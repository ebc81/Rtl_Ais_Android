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
package com.wordpress.ebc81.rtl_ais_android.core;

import android.util.Log;

import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;


public class RtlAisJava {
    private final static String TAG = "RTLAISJAVA";

    final static int LIBUSB_ERROR_IO = -1;
    final static int LIBUSB_ERROR_INVALID_PARAM = -2;
    final static int LIBUSB_ERROR_ACCESS = -3;
    final static int LIBUSB_ERROR_NO_DEVICE = -4;
    final static int LIBUSB_ERROR_NOT_FOUND = -5;
    final static int LIBUSB_ERROR_BUSY = -6;
    final static int LIBUSB_ERROR_TIMEOUT = -7;
    final static int LIBUSB_ERROR_OVERFLOW = -8;
    final static int LIBUSB_ERROR_PIPE = -9;
    final static int LIBUSB_ERROR_INTERRUPTED = -10;
    final static int LIBUSB_ERROR_NO_MEM = -11;
    final static int LIBUSB_ERROR_NOT_SUPPORTED = -12;
    final static int LIBUSB_ERROR_OTHER = -99;

    final static int EXIT_OK = 0;
    final static int EXIT_WRONG_ARGS = 1;
    final static int EXIT_INVALID_FD = 2;
    final static int EXIT_NO_DEVICES = 3;
    final static int EXIT_FAILED_TO_OPEN_DEVICE = 4;
    final static int EXIT_CANNOT_RESTART = 5;
    final static int EXIT_CANNOT_CLOSE = 6;
    final static int EXIT_UNKNOWN = 7;
    final static int EXIT_SIGNAL_CAUGHT = 8 ;
    final static int EXIT_NOT_ENOUGH_POWER = 9;
    final static int EXIT_AIS_DECODERR = 10;

    private final static Object locker = new Object();
    private final static Object exitcode_locker = new Object();
    private final static ArrayList<OnProcessTalkCallback> talk_callacks = new ArrayList<RtlAisJava.OnProcessTalkCallback>();

    private static volatile AtomicInteger exitcode = new AtomicInteger(EXIT_UNKNOWN);
    private static volatile AtomicBoolean exitcode_set = new AtomicBoolean(false);

    static {
        System.loadLibrary("RtlAisJava");
    }

    private static native void open(final String args, final int fd, final String uspfs_path);// throws RtlTcpException;
    private static native void close();// throws RtlTcpException;
    public static native boolean isNativeRunning();

    private static void printf_receiver(final String data) {
        for (final OnProcessTalkCallback c : talk_callacks)
            c.OnProcessTalk(data);
        Log.d(TAG, data);
    }

    private static void printf_stderr_receiver(final String data) {
        for (final OnProcessTalkCallback c : talk_callacks)
            c.OnProcessTalk(data);
        Log.w(TAG, data);
    }

    private static void onclose(int exitcode) {
        RtlAisJava.exitcode.set(exitcode);
        exitcode_set.set(true);
        synchronized (exitcode_locker) {
            exitcode_locker.notifyAll();
        }
        if (exitcode != EXIT_OK)
            Log.e(TAG, "Exitcode "+exitcode);
        else
            Log.d(TAG, "Exited with success");
    }

    private static void onopen() {
        for (final OnProcessTalkCallback c : talk_callacks)
            c.OnOpened();
        Log.d(TAG, "Device open");
    }

    public static void registerWordCallback(final OnProcessTalkCallback callback) {
        if (!talk_callacks.contains(callback)) talk_callacks.add(callback);
    }

    public static void unregisterWordCallback(final OnProcessTalkCallback callback) {
        talk_callacks.remove(callback);
    }


    public static void start(final String args, final int fd, final String uspfs_path) throws RtlException {
        if (isNativeRunning()) {
            close();
            try {
                synchronized (locker) {
                    locker.wait(5000);
                }
            } catch (InterruptedException e) {}

            if (isNativeRunning()) throw new RtlException(EXIT_CANNOT_RESTART);
        }

        new Thread() {
            public void run() {
                exitcode_set.set(false);
                exitcode.set(EXIT_UNKNOWN);

                open(args, fd, uspfs_path);

                if (!exitcode_set.get()) {
                    close();
                    try {
                        synchronized (exitcode_locker) {
                            exitcode_locker.wait(1000);
                        }
                    } catch (InterruptedException e) {}
                }

                if (!exitcode_set.get())
                    exitcode.set(EXIT_CANNOT_CLOSE);

                RtlException e = null;
                final int exitcode = RtlAisJava.exitcode.get();
                if (exitcode != EXIT_OK) e = new RtlException(exitcode);

                for (final OnProcessTalkCallback c : talk_callacks)
                    c.OnClosed(exitcode, e);

                synchronized (locker) {
                    locker.notifyAll();
                }
            };
        }.start();
    }

    public static void stop() {
        if (!isNativeRunning()) return;
        close();
    }

    public static interface OnProcessTalkCallback {
        /** Whenever the process writes something to its stdout, this will get called */
        void OnProcessTalk(final String line);

        void OnClosed(final int exitvalue, final RtlException e);

        void OnOpened();
    }

}
