# ESP-IDF + ESP-ADF build environment shared by the ESP32 firmware projects in
# this repo (roomsensor, espamp, surprise). Use it via the `idf` wrapper
# next to this file.
ARG IDF_VERSION=v5.3.2
ARG NODE_VERSION=22.12.0

FROM node:${NODE_VERSION}-bookworm-slim AS node

FROM espressif/idf:${IDF_VERSION}
ARG IDF_VERSION

# Match the host user so files written into the mounted repo stay owned by you.
ARG USER_UID=1000
ARG USER_GID=1000
# ESP-ADF release matching IDF_VERSION (v2.7 supports IDF release/v5.3).
ARG ADF_VERSION=v2.7

ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8

RUN apt-get update -y \
    && apt-get install -y --no-install-recommends jq udev \
    && rm -rf /var/lib/apt/lists/*

# Node is needed for roomsensor's web UI (js/), which is built into the LittleFS image.
COPY --from=node /usr/local/bin/node /usr/local/bin/node
COPY --from=node /usr/local/lib/node_modules /usr/local/lib/node_modules
RUN ln -s ../lib/node_modules/npm/bin/npm-cli.js /usr/local/bin/npm \
    && ln -s ../lib/node_modules/npm/bin/npx-cli.js /usr/local/bin/npx

# The repo and /opt/esp/idf are owned by different uids inside the container;
# the CMake version scripts call git, so trust every directory.
RUN git config --system --add safe.directory '*'

# ESP-ADF for I2S audio. Its bundled esp-idf submodule is skipped in favour
# of the image's IDF, which gets ADF's FreeRTOS patch (allows task stacks in PSRAM;
# harmless for projects that don't use ADF). Projects opt in with
# include($ENV{ADF_PATH}/CMakeLists.txt) before project.cmake.
ENV ADF_PATH=/opt/esp/adf
RUN git clone --depth 1 -b ${ADF_VERSION} https://github.com/espressif/esp-adf.git ${ADF_PATH} \
    && git -C ${ADF_PATH} -c submodule.esp-idf.update=none submodule update --init --recursive --depth 1 \
    && IDF_MINOR=$(echo "${IDF_VERSION}" | grep -oE '^v[0-9]+\.[0-9]+') \
    && git -C ${IDF_PATH} apply ${ADF_PATH}/idf_patches/idf_${IDF_MINOR}_freertos.patch

COPY entrypoint.sh /usr/local/bin/idf-entrypoint.sh

RUN groupadd -o -g ${USER_GID} builder \
    && useradd -o -m -u ${USER_UID} -g ${USER_GID} -G dialout -d /home/builder -s /bin/bash builder \
    && mkdir -p /home/builder/.cache \
    && chown -R ${USER_UID}:${USER_GID} /home/builder \
    && echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> /home/builder/.bashrc

USER builder

ENTRYPOINT [ "/usr/local/bin/idf-entrypoint.sh" ]
CMD [ "/bin/bash" ]
