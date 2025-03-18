#syntax docker/dockerfile:1.10

FROM ubuntu
FROM drogonframework/drogon
LABEL authors="Contarr"

RUN apt-get update && \
    apt-get install -y \
    clang \
    ladspa-sdk \
    freeglut3-dev \
    g++ \
    libasound2-dev \
    libcurl4-openssl-dev \
    libfreetype6-dev \
    libjack-jackd2-dev \
    libx11-dev \
    libxcomposite-dev \
    libjsoncpp-dev \
    mesa-common-dev \
    ninja-build \
    webkit2gtk-4.0

RUN ln -s /usr/include/jsoncpp/json/ /usr/include/json

WORKDIR /app

RUN drogon_ctl create project worker

WORKDIR /app/worker

RUN cp CMakeLists.txt ..

COPY . /app/worker

RUN cp -f ../CMakeLists.txt ./

RUN cd build && cmake .. -D CMAKE_BUILD_TYPE=Release && make -j4

RUN ls ./build/

CMD ["./build/worker"]