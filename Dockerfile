FROM eclipse-temurin:17-jdk-jammy

ENV ANDROID_HOME=/opt/android-sdk \
    ANDROID_SDK_ROOT=/opt/android-sdk \
    GRADLE_VERSION=8.9 \
    GRADLE_USER_HOME=/home/translator/.gradle

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl unzip git python3 python3-pip \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 translator \
    && useradd --uid 10001 --gid translator --create-home translator \
    && mkdir -p /opt/gradle /opt/android-sdk /workspace \
    && chown -R translator:translator /opt/gradle /opt/android-sdk /workspace /home/translator

RUN curl -fsSL "https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip" -o /tmp/gradle.zip \
    && unzip -q /tmp/gradle.zip -d /opt/gradle \
    && rm /tmp/gradle.zip \
    && ln -s "/opt/gradle/gradle-${GRADLE_VERSION}/bin/gradle" /usr/local/bin/gradle

WORKDIR /workspace
USER translator

CMD ["bash"]
