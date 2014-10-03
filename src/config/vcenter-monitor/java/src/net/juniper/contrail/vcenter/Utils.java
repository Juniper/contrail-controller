package net.juniper.contrail.vcenter;

import java.math.BigInteger;
import java.util.Collections;

public class Utils {
    public static byte[] parseMacAddress(String macAddress) {
        String[] bytes = macAddress.split(":");
        byte[] parsed = new byte[bytes.length];
        for (int x = 0; x < bytes.length; x++) {
            BigInteger temp = new BigInteger(bytes[x], 16);
            byte[] raw = temp.toByteArray();
            parsed[x] = raw[raw.length - 1];
        }
        return parsed;
    }

    public static <T> Iterable<T> safe(Iterable<T> iterable) {
        return iterable == null ? Collections.<T>emptyList() : iterable;
    }
}
