FROM fedora:43 AS fedora-tools
RUN dnf -y --setopt=install_weak_deps=False install \
    gcc-c++ cmake ninja-build SDL3-devel libglvnd-devel rpm-build squashfs-tools zip \
    mingw64-gcc-c++ mingw64-SDL3 mingw64-libepoxy mingw32-nsis \
    && dnf clean all

FROM debian:13-slim AS debian-tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake ninja-build libsdl3-dev libglx-dev libopengl-dev dpkg-dev file \
    && rm -rf /var/lib/apt/lists/*

FROM scratch AS sources
COPY --from=huynity CMakeLists.txt LICENSE /src/Huynity/
COPY --from=huynity engine /src/Huynity/engine
COPY --from=huynity adapters /src/Huynity/adapters
COPY CMakeLists.txt LICENSE /src/Agario3D/
COPY game /src/Agario3D/game
COPY assets /src/Agario3D/assets
COPY packaging /src/Agario3D/packaging

FROM debian-tools AS deb
COPY --from=sources /src /src
WORKDIR /src/Agario3D
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build /build -j2 --target agario3d \
    && cpack --config /build/CPackConfig.cmake -G DEB

FROM fedora-tools AS linux
RUN dnf -y --setopt=install_weak_deps=False install libstdc++-static && dnf clean all
ENV APPIMAGE_RUNTIME=/opt/appimage-runtime
ADD --checksum=sha256:2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d \
    https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 /opt/appimage-runtime
COPY --from=sources /src /src
WORKDIR /src/Agario3D
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
        "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" \
    && cmake --build /build -j2 --target package-portable \
    && cpack --config /build/CPackConfig.cmake -G RPM

FROM fedora-tools AS windows
COPY --from=sources /src /src
WORKDIR /src/Agario3D
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
        -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
    && cmake --build /build -j2 --target agario3d \
    && cpack --config /build/CPackConfig.cmake

FROM scratch
COPY --from=linux /build/packages/*.rpm /build/packages/*.AppImage /build/packages/*.zip /
COPY --from=deb /build/packages/*.deb /
COPY --from=windows /build/packages/*.exe /build/packages/*.zip /
