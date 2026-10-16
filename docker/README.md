# JDK build container

The root `Dockerfile` provides OpenJDK 17 and Gradle 8.9. It is intended to make Android Java/Gradle builds consistent across development machines while keeping generated artifacts outside version control.

```sh
docker compose build jdk
docker compose run --rm jdk gradle -p launcher assembleDebug
```

To use the Gradle wrapper instead:

```sh
docker compose run --rm jdk ./launcher/gradlew -p launcher assembleDebug
```

The container does not install the Android SDK/NDK. Provide the SDK at `/opt/android-sdk` (or mount the host SDK there), accept the Android SDK licenses on the host, and supply a local `launcher/local.properties` with its SDK path when required. `adb` and USB device access remain host-side tasks; the container does not receive host device privileges by default.

A mounted Gradle cache is kept in the named `gradle-cache` volume. Remove that volume only when you intentionally want to discard cached dependencies.
