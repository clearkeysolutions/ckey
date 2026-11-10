# Applied to apps that use org.openckey:ckey.
# JNI binds by name: keep the native bridge class and its native methods.
-keep class org.openckey.ckey.CKeyNative { native <methods>; }
# Constructed from native code via (ILjava/lang/String;)V.
-keep class org.openckey.ckey.CKeyException { <init>(int, java.lang.String); }
