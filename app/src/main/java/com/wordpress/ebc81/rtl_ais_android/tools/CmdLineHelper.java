package com.wordpress.ebc81.rtl_ais_android.tools;

/**
 * Created by ebc on 27.08.2015.
 */
public class CmdLineHelper {

    public static final int RTL_AIS_ANDROID_CMD_START       = 1;
    public static final int RTL_AIS_ANDROID_CMD_STOP        = 2;
    public static final int RTL_AIS_ANDROID_CMD_STATUS      = 3;
    public static final int RTL_AIS_ANDROID_CMD_UNKNOWN     = 4;

    public static final String exit_argument    = "-exit";
    public static final String status_argument  = "-status";

    public static int getCmd(String argument)
    {
        if (argument.isEmpty())
            return RTL_AIS_ANDROID_CMD_UNKNOWN;
        if ( argument.equals(exit_argument) )
            return  RTL_AIS_ANDROID_CMD_STOP;
        if ( argument.equals(status_argument))
            return RTL_AIS_ANDROID_CMD_STATUS;
        return RTL_AIS_ANDROID_CMD_START;

    }
}
