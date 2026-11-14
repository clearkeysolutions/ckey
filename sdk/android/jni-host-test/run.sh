#!/usr/bin/env bash
# Host-JVM verification of the Android JNI bridge (no Android SDK/NDK needed).
#
#  1. Generates a Java mirror of CKeyNative.kt's `@JvmStatic external fun`s
#     and runs `javac -h` on it; ckey_jni.cpp is then compiled with that
#     header force-included, so any JNI name or parameter-type mismatch
#     between Kotlin and C++ is a compile error ("conflicting declaration").
#  2. Builds libckey_jni.so for the host from the repository's core sources
#     (same file list and switches as src/main/cpp/CMakeLists.txt) and checks
#     every declared native method is exported.
#  3. Runs the Java harness (encode -> render RGBA -> gray -> decode, styles,
#     exports, telemetry) against the mirror via System.load.
#  4. If a Kotlin compiler is available (KOTLINC_JAR, or the one bundled with
#     Gradle), compiles the real Kotlin sources (against tiny android.* stubs),
#     reruns the harness against the Kotlin-compiled CKeyNative, runs the
#     Kotlin-API checks and the JUnit unit tests in ckey/src/test.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ANDROID_DIR="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$ANDROID_DIR/../.." && pwd)"
OUT="${OUT:-$HERE/build}"
JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}"
CXX="${CXX:-g++}"
KT_SRC="$ANDROID_DIR/ckey/src/main/kotlin"
JNI_SRC="$ANDROID_DIR/ckey/src/main/cpp/ckey_jni.cpp"
rm -rf "$OUT" && mkdir -p "$OUT"

echo "== 1. JNI signatures (Kotlin -> javac -h -> g++)"
python3 "$HERE/gen_mirror.py" "$KT_SRC/org/openckey/ckey/CKeyNative.kt" "$OUT/mirror-src"
"$JAVA_HOME/bin/javac" -d "$OUT/mirror" -h "$OUT/jni-h" \
  "$OUT/mirror-src/org/openckey/ckey/CKeyNative.java" "$OUT/mirror-src/org/openckey/ckey/CKeyException.java"
# Linux (GNU ld) or macOS (ld64): the CI runner is a self-hosted Mac.
if [ "$(uname -s)" = Darwin ]; then
  JNI_OS=darwin
  LINK_EXPORTS=(-Wl,-exported_symbol,_Java_* -Wl,-exported_symbol,_JNI_OnLoad -Wl,-dead_strip)
  exports_of() { nm -gU "$1" | awk '{print $3}' | sed 's/^_//'; }
else
  JNI_OS=linux
  LINK_EXPORTS=(-Wl,--version-script="$ANDROID_DIR/ckey/src/main/cpp/ckey_jni.map" -Wl,--gc-sections)
  exports_of() { nm -D --defined-only "$1" | awk '{print $3}'; }
fi
JNI_INC=(-I"$JAVA_HOME/include" -I"$JAVA_HOME/include/$JNI_OS" -I"$REPO/include" -I"$REPO/src")
CERTIFY=0
SOURCES=("$REPO/src/ckey_codec.cpp" "$REPO/src/ckey_scan.cpp" "$REPO/src/ckey_render.cpp" "$REPO/src/telemetry.cpp")
if [ -f "$REPO/src/ckey_certify.cpp" ]; then CERTIFY=1; SOURCES+=("$REPO/src/ckey_certify.cpp"); fi
DEFS=(-DCKEY_JNI_SCAN=1 -DCKEY_JNI_RENDER=1 -DCKEY_JNI_TELEMETRY=1 -DCKEY_JNI_CERTIFY=$CERTIFY)
"$CXX" -std=c++20 -Wall -Wextra -Werror -fsyntax-only "${JNI_INC[@]}" "${DEFS[@]}" \
  -include "$OUT/jni-h/org_openckey_ckey_CKeyNative.h" "$JNI_SRC"
for variant in "-DCKEY_JNI_SCAN=0" "-DCKEY_JNI_RENDER=0" "-DCKEY_JNI_TELEMETRY=0"; do
  "$CXX" -std=c++20 -Wall -Wextra -Werror -fsyntax-only "${JNI_INC[@]}" $variant "$JNI_SRC"
done
echo "signatures match; module-off variants compile"

echo "== 2. host libckey_jni.so"
mkdir -p "$OUT/obj"
OBJS=()
for s in "${SOURCES[@]}"; do
  o="$OUT/obj/$(basename "$s" .cpp).o"
  "$CXX" -std=c++20 -O2 -fPIC -fvisibility=hidden -Wno-unused-function -I"$REPO/include" -I"$REPO/src" -c "$s" -o "$o" &
  OBJS+=("$o")
done
"$CXX" -std=c++20 -O2 -fPIC -fvisibility=hidden -Wall -Wextra "${JNI_INC[@]}" "${DEFS[@]}" -c "$JNI_SRC" -o "$OUT/obj/ckey_jni.o" &
wait
"$CXX" -shared -o "$OUT/libckey_jni.so" "$OUT/obj/ckey_jni.o" "${OBJS[@]}" \
  "${LINK_EXPORTS[@]}" -pthread
declared=$(grep -o 'native [^ ]* [A-Za-z]*(' "$OUT/mirror-src/org/openckey/ckey/CKeyNative.java" | awk '{print $3}' | tr -d '(' | sort)
exported=$(exports_of "$OUT/libckey_jni.so" | grep '^Java_' | sed 's/^Java_org_openckey_ckey_CKeyNative_//' | sort)
if [ "$declared" != "$exported" ]; then
  echo "declared vs exported native methods differ:"; diff <(echo "$declared") <(echo "$exported") || true; exit 1
fi
others=$(exports_of "$OUT/libckey_jni.so" | grep -v -e '^Java_' -e '^JNI_OnLoad$' || true)
[ -z "$others" ] || { echo "unexpected exports: $others"; exit 1; }
echo "exports: JNI_OnLoad + $(echo "$exported" | wc -l) native methods, nothing else (certify=$CERTIFY)"

echo "== 3. Java harness against the generated mirror"
"$JAVA_HOME/bin/javac" -cp "$OUT/mirror" -d "$OUT/harness" "$HERE/java/HostHarness.java"
"$JAVA_HOME/bin/java" -Xcheck:jni -Dorg.openckey.ckey.library="$OUT/libckey_jni.so" -Dorg.openckey.ckey.vectors="$REPO/tests/vectors/safe-actions-v1.json" \
  -cp "$OUT/mirror:$OUT/harness" HostHarness 2>&1 | grep -v -e '^WARNING' -e '^Picked up' | tail -n 3

echo "== 4. Kotlin sources"
if [ -z "${KOTLINC_JAR:-}" ]; then
  GRADLE_LIB="$(dirname "$(readlink -f "$(command -v gradle 2>/dev/null || echo /nonexistent)")")/../lib"
  KOTLINC_JAR="$(ls "$GRADLE_LIB"/kotlin-compiler-embeddable-*.jar 2>/dev/null | head -n1 || true)"
fi
if [ -z "$KOTLINC_JAR" ] || [ ! -f "$KOTLINC_JAR" ]; then
  echo "skip: no Kotlin compiler (set KOTLINC_JAR or put Gradle on PATH)"; exit 0
fi
LIB="$(dirname "$KOTLINC_JAR")"
# Optional jars may be missing from a Gradle distribution; never fail on that.
jar_of() { { ls "$LIB"/$1-*.jar 2>/dev/null || true; } | head -n1; }
KCP="$KOTLINC_JAR:$(jar_of kotlin-stdlib):$(jar_of kotlin-script-runtime):$(jar_of kotlin-reflect):$(jar_of kotlin-daemon-embeddable):$(jar_of trove4j):$(jar_of annotations):$(jar_of kotlinx-coroutines-core-jvm)"
STDLIB="$(jar_of kotlin-stdlib)"
JUNIT="$(jar_of junit):$(jar_of hamcrest-core)"
kotlinc() { "$JAVA_HOME/bin/java" -cp "$KCP" org.jetbrains.kotlin.cli.jvm.K2JVMCompiler -no-stdlib -no-reflect -jvm-target 17 "$@"; }
"$JAVA_HOME/bin/javac" -d "$OUT/stubs" $(find "$HERE/stubs" -name '*.java')
# Everything in the core module except CKeyView.kt (android.view).
MAIN_KT=$(find "$KT_SRC" -name '*.kt' ! -name 'CKeyView.kt')
kotlinc -module-name ckey_release -cp "$STDLIB:$OUT/stubs" -d "$OUT/kotlin-main" $MAIN_KT 2>&1 | grep -v '^warning' || true
[ -f "$OUT/kotlin-main/org/openckey/ckey/CKeyNative.class" ] || { echo "Kotlin compile failed"; exit 1; }
# The Kotlin-compiled class must have exactly the mirror's static native methods and descriptors.
natives() {
  "$JAVA_HOME/bin/javap" -cp "$1" -s org.openckey.ckey.CKeyNative 2>/dev/null > "$OUT/javap.txt"
  if grep ' native ' "$OUT/javap.txt" | grep -qv ' static '; then echo "non-static native method in $1" >&2; return 1; fi
  grep -A1 ' native ' "$OUT/javap.txt" | grep -o -e '[A-Za-z0-9_]*(' -e 'descriptor: .*' | paste - - | sort
}
natives "$OUT/kotlin-main" > "$OUT/natives-kotlin.txt"
natives "$OUT/mirror" > "$OUT/natives-mirror.txt"
diff "$OUT/natives-mirror.txt" "$OUT/natives-kotlin.txt" || { echo "Kotlin and mirror native descriptors differ"; exit 1; }
echo "Kotlin CKeyNative: $(wc -l < "$OUT/natives-kotlin.txt") static native methods, descriptors identical to the mirror"
RUN_CP="$STDLIB:$OUT/stubs:$OUT/kotlin-main"
"$JAVA_HOME/bin/javac" -cp "$RUN_CP" -d "$OUT/harness-kt" "$HERE/java/HostHarness.java" "$HERE/java/KotlinApiCheck.java"
echo "-- harness against the Kotlin-compiled CKeyNative"
"$JAVA_HOME/bin/java" -Xcheck:jni -Dorg.openckey.ckey.library="$OUT/libckey_jni.so" -Dorg.openckey.ckey.vectors="$REPO/tests/vectors/safe-actions-v1.json" -cp "$RUN_CP:$OUT/harness-kt" HostHarness 2>&1 | grep -v -e '^WARNING' -e '^Picked up' | tail -n 1
echo "-- Kotlin public API from Java"
"$JAVA_HOME/bin/java" -Xcheck:jni -Dorg.openckey.ckey.library="$OUT/libckey_jni.so" -Dorg.openckey.ckey.vectors="$REPO/tests/vectors/safe-actions-v1.json" -cp "$RUN_CP:$OUT/harness-kt" KotlinApiCheck 2>&1 | grep -v -e '^WARNING' -e '^Picked up' | tail -n 4
echo "-- JUnit unit tests (ckey/src/test)"
kotlinc -module-name ckey_test -Xfriend-paths="$OUT/kotlin-main" -cp "$STDLIB:$OUT/stubs:$OUT/kotlin-main:$JUNIT" -d "$OUT/kotlin-test" \
  $(find "$ANDROID_DIR/ckey/src/test" -name '*.kt') 2>&1 | grep -v '^warning' || true
[ -d "$OUT/kotlin-test" ] || { echo "Kotlin test compile failed"; exit 1; }
TESTS=$(cd "$OUT/kotlin-test" && find . -name '*Test.class' | sed 's|^\./||; s|\.class$||; s|/|.|g' | sort)
"$JAVA_HOME/bin/java" -cp "$RUN_CP:$OUT/kotlin-test:$JUNIT" org.junit.runner.JUnitCore $TESTS > "$OUT/junit.txt" 2>&1 \
  || { cat "$OUT/junit.txt"; exit 1; }
grep -E '^OK' "$OUT/junit.txt"
