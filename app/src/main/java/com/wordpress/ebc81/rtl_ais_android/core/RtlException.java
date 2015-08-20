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


import com.wordpress.ebc81.rtl_ais_android.R;
import com.wordpress.ebc81.rtl_ais_android.tools.StrRes;
import com.wordpress.ebc81.rtl_ais_android.tools.RtlStartException;
import com.wordpress.ebc81.rtl_ais_android.tools.RtlStartException.err_info;

public class RtlException extends Exception {
    public final int id;

    static final String translateToString(final int id) {
        switch (id) {
            case RtlAisJava.LIBUSB_ERROR_IO:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_IO");
            case RtlAisJava.LIBUSB_ERROR_INVALID_PARAM :
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_INVALID_PARAM");
            case RtlAisJava.LIBUSB_ERROR_ACCESS:
                return StrRes.get(R.string.exception_LIBUSB_ERROR_ACCESS);
            case RtlAisJava.LIBUSB_ERROR_NO_DEVICE:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_NO_DEVICE");
            case RtlAisJava.LIBUSB_ERROR_NOT_FOUND:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_NOT_FOUND");
            case RtlAisJava.LIBUSB_ERROR_BUSY:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_BUSY");
            case RtlAisJava.LIBUSB_ERROR_TIMEOUT:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_TIMEOUT");
            case RtlAisJava.LIBUSB_ERROR_OVERFLOW:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_OVERFLOW");
            case RtlAisJava.LIBUSB_ERROR_PIPE:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_PIPE");
            case RtlAisJava.LIBUSB_ERROR_INTERRUPTED:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_INTERRUPTED");
            case RtlAisJava.LIBUSB_ERROR_NO_MEM:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_NO_MEM");
            case RtlAisJava.LIBUSB_ERROR_NOT_SUPPORTED:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_NOT_SUPPORTED");
            case RtlAisJava.LIBUSB_ERROR_OTHER:
                return StrRes.get(R.string.exception_LIBUSB_GENERIC, "LIBUSB_ERROR_OTHER");

            case RtlAisJava.EXIT_OK:
                return StrRes.get(R.string.exception_OK);
            case RtlAisJava.EXIT_WRONG_ARGS:
                return StrRes.get(R.string.exception_WRONG_ARGS);
            case RtlAisJava.EXIT_INVALID_FD:
                return StrRes.get(R.string.exception_INVALID_FD);
            case RtlAisJava.EXIT_NO_DEVICES:
                return StrRes.get(R.string.exception_NO_DEVICES);
            case RtlAisJava.EXIT_FAILED_TO_OPEN_DEVICE:
                return StrRes.get(R.string.exception_FAILED_TO_OPEN_DEVICE);
            case RtlAisJava.EXIT_CANNOT_RESTART:
                return StrRes.get(R.string.exception_CANNOT_RESTART);
            case RtlAisJava.EXIT_CANNOT_CLOSE:
                return StrRes.get(R.string.exception_CANNOT_CLOSE);
            case RtlAisJava.EXIT_UNKNOWN:
                return StrRes.get(R.string.exception_UNKNOWN);
            case RtlAisJava.EXIT_SIGNAL_CAUGHT:
                return StrRes.get(R.string.exception_SIGNAL_CAUGHT);
            case RtlAisJava.EXIT_NOT_ENOUGH_POWER:
                return StrRes.get(R.string.exception_NOT_ENOUGH_POWER);
            case RtlAisJava.EXIT_AIS_DECODERR:
                return StrRes.get(R.string.exception_AIS_DECODER);



            default:
                return StrRes.get(R.string.exception_DEFAULT, id);
        }
    }

    public final RtlStartException.err_info getReason() {
        switch (id) {
            case RtlAisJava.LIBUSB_ERROR_IO:
                return err_info.no_devices_found;
            case RtlAisJava.LIBUSB_ERROR_INVALID_PARAM:
                return err_info.unknown_error;
            case RtlAisJava.LIBUSB_ERROR_ACCESS:
                return err_info.permission_denied;
            case RtlAisJava.LIBUSB_ERROR_NO_DEVICE:
                return err_info.no_devices_found;
            case RtlAisJava.LIBUSB_ERROR_NOT_FOUND:
                return err_info.no_devices_found;
            case RtlAisJava.LIBUSB_ERROR_BUSY:
                return err_info.already_running;
            case RtlAisJava.LIBUSB_ERROR_TIMEOUT:
                return err_info.no_devices_found;
            case RtlAisJava.LIBUSB_ERROR_OVERFLOW:
                return err_info.no_devices_found;
            case RtlAisJava.LIBUSB_ERROR_PIPE:
                return err_info.unknown_error;
            case RtlAisJava.LIBUSB_ERROR_INTERRUPTED:
                return err_info.unknown_error;
            case RtlAisJava.LIBUSB_ERROR_NO_MEM:
                return err_info.unknown_error;
            case RtlAisJava.LIBUSB_ERROR_NOT_SUPPORTED:
                return err_info.no_devices_found;
            case RtlAisJava.LIBUSB_ERROR_OTHER:
                return err_info.unknown_error;

            case RtlAisJava.EXIT_OK:
                return null;
            case RtlAisJava.EXIT_WRONG_ARGS:
                return err_info.unknown_error;
            case RtlAisJava.EXIT_INVALID_FD:
                return err_info.permission_denied;
            case RtlAisJava.EXIT_NO_DEVICES:
                return err_info.no_devices_found;
            case RtlAisJava.EXIT_FAILED_TO_OPEN_DEVICE:
                return err_info.no_devices_found;
            case RtlAisJava.EXIT_CANNOT_RESTART:
                return err_info.unknown_error;
            case RtlAisJava.EXIT_CANNOT_CLOSE:
                return err_info.replug;
            case RtlAisJava.EXIT_UNKNOWN:
                return err_info.unknown_error;
            case RtlAisJava.EXIT_SIGNAL_CAUGHT:
                return err_info.unknown_error;
            case RtlAisJava.EXIT_NOT_ENOUGH_POWER:
                return err_info.unknown_error;
            case RtlAisJava.EXIT_AIS_DECODERR:
                return err_info.unknown_error;

            default:
                return null;
        }
    }

    public RtlException(final int id) {
        super(translateToString(id));
        this.id = id;
    }
}

