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

import android.annotation.SuppressLint;
import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

import com.wordpress.ebc81.rtl_ais_android.R;
import com.wordpress.ebc81.rtl_ais_android.StreamActivity;
import com.wordpress.ebc81.rtl_ais_android.core.RtlAisJava;
import com.wordpress.ebc81.rtl_ais_android.core.RtlException;

import java.util.ArrayList;

public class BinaryRunnerService extends Service {


    private static final String TAG = "rtl_ais_android";

    private final static int ONGOING_NOTIFICATION_ID = 2;

    private RtlAisJava.OnProcessTalkCallback callback1;

    private final ArrayList<ExceptionListener> exception_listeners = new ArrayList<BinaryRunnerService.ExceptionListener>();
    private PowerManager.WakeLock wl = null;

    private final ArrayList<Exception> accummulated_errors = new ArrayList<Exception>();

    private final IBinder mBinder = new LocalBinder();

    public class LocalBinder extends Binder {

        public BinaryRunnerService getService(final ExceptionListener listener) {
            if (!exception_listeners.contains(listener)) exception_listeners.add(listener);
            for (Exception e : accummulated_errors) {
                try {
                    listener.onException(e);
                } catch (Throwable t) {};
            }
            return BinaryRunnerService.this;
        }
    }

    @Override
    public IBinder onBind(Intent arg0) {
        return mBinder;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    public void start(final String args, final int fd, final String uspfs_path) {
        try {

            Log.i(TAG, "--------------------Rtl Ais driver START");

            if (args == null) {
                LogRtlAis.appendLine("Service did receive null argument.");
                stopForeground(true);
                System.exit(0);
                return;
            }

            if ( args.equals("-exit") ) {
                Log.i(TAG, "Rtl Ais driver will be closed");
                LogRtlAis.appendLine("Service did receive exit argument");
                stopForeground(true);
                System.exit(0);
                return;
            }

            accummulated_errors.clear();

            if (RtlAisJava.isNativeRunning()) {
                LogRtlAis.appendLine("Service is running. Stopping... You can safely start it again now.");
                final Exception e = new Exception("Service is running. Stopping...");
                for (final ExceptionListener listener : exception_listeners) listener.onException(e);
                if (e != null) accummulated_errors.add(e);
                RtlAisJava.stop();
                try {
                    Thread.sleep(500);
                } catch (Throwable t) {}
                stopForeground(true);
                System.exit(0);
                return;
            }

            // close any previous attempts and try to reopen


            try {
                wl = null;
                wl = ((PowerManager)getSystemService(
                        Context.POWER_SERVICE)).newWakeLock(
                        PowerManager.SCREEN_BRIGHT_WAKE_LOCK
                                | PowerManager.ON_AFTER_RELEASE,
                        TAG);
                wl.acquire();
                LogRtlAis.appendLine("Acquired wake lock. Will keep the screen on.");
            } catch (Throwable e) {e.printStackTrace();}

            LogRtlAis.appendLine("#rtl_tcp_andro " + args);

            RtlAisJava.unregisterWordCallback(callback1);
            RtlAisJava.registerWordCallback(callback1 = new RtlAisJava.OnProcessTalkCallback() {

                @Override
                public void OnProcessTalk(final String line) {
                    LogRtlAis.appendLine("rtl-ais: " + line + "\n");
                }

                @Override
                public void OnClosed(int exitvalue, final RtlException e) {
                    if (e != null)
                        LogRtlAis.appendLine("Exit message: " + e.getMessage() + "\n");
                    else
                        LogRtlAis.appendLine("Exit code: " + exitvalue + "\n");

                    for (final ExceptionListener listener : exception_listeners) listener.onException(e);
                    if (e != null) accummulated_errors.add(e);

                    stopSelf();
                }

                @Override
                public void OnOpened() {
                    for (final ExceptionListener listener : exception_listeners) listener.onStarted();
                        LogRtlAis.announceStateChanged(true);
                }


            });

            RtlAisJava.start(args, fd, uspfs_path);

        } catch (Exception e) {
            for (final ExceptionListener listener : exception_listeners) listener.onException(e);
            e.printStackTrace();
            accummulated_errors.add(e);
        }

        makeForegroundNotification();
    }

    //@SuppressWarnings("deprecation")
    private void makeForegroundNotification() {
        try {
            final PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, new Intent(this, StreamActivity.class), 0);
            Notification notification = new Notification.Builder(this)
                    .setContentIntent(pendingIntent)
                    .setContentTitle(getText(R.string.notif_title))
                    .setContentText(getText(R.string.notif_message))
                    .setSmallIcon(android.R.drawable.ic_media_play)
                    .build(); // available from API level 11 and onwards


           // Notification notification = new Notification(android.R.drawable.ic_media_play, getText(R.string.notif_title), System.currentTimeMillis());

           // notification.setLatestEventInfo(this, getText(R.string.notif_title),
            //        getText(R.string.notif_message), pendingIntent);
            startForeground(ONGOING_NOTIFICATION_ID, notification);
        } catch (Throwable t) {
            t.printStackTrace();
        }
    }

    public void stop() {
        stopSelf();
    }

    public void unregisterListener(final ExceptionListener listener) {
        exception_listeners.remove(listener);
    }

    @SuppressLint("NewApi")
    @Override
    public void onDestroy() {
        Log.i(TAG,"Rtl Ais driver onDestroy");
        RtlAisJava.stop();

        stopForeground(true);

        LogRtlAis.announceStateChanged(false);
        for (final ExceptionListener listener : exception_listeners) listener.onException(null);

        RtlAisJava.unregisterWordCallback(callback1);

        try {
            wl.release();
            LogRtlAis.appendLine("Wake lock released");
        } catch (Throwable t) {};

        super.onDestroy();
    }

    public static interface ExceptionListener {
        public void onException(final Exception e);
        public void onStarted();
    }
}
