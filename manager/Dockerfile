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
    webkit2gtk-4.0 \
    libboost-all-dev \
    gnupg \
    pkg-config \
    libssl-dev \
    libsasl2-dev \
    libzstd-dev \
    libz-dev \
    libsnappy-dev \
    liblz4-dev \
    cmake \
    libmongoc-1.0 \
    build-essential  \
    libboost-system-dev  \
    zlib1g-dev  \
    git


# Install MongoDB C++ driver (mongocxx and bsoncxx)
WORKDIR /tmp
RUN curl -OL https://github.com/mongodb/mongo-cxx-driver/releases/download/r4.0.0/mongo-cxx-driver-r4.0.0.tar.gz && \
    tar -xzf mongo-cxx-driver-r4.0.0.tar.gz &&  \
    cd mongo-cxx-driver-r4.0.0/build &&         \
    cmake ..                                    \
        -DCMAKE_BUILD_TYPE=Release              \
        -DMONGOCXX_OVERRIDE_DEFAULT_INSTALL_PREFIX=OF && \
    cmake --build . &&                          \
    sudo cmake --build . --target install

RUN ln -s /usr/include/jsoncpp/json/ /usr/include/json

RUN cp -r /usr/local/include/mongocxx/v_noabi/mongocxx/* /usr/local/include/mongocxx/ && \
    rm -rf /usr/local/include/mongocxx/v_noabi

RUN cp -r /usr/local/include/bsoncxx/v_noabi/bsoncxx/* /usr/local/include/bsoncxx/ && \
    rm -rf /usr/local/include/bsoncxx/v_noabi

RUN ls /usr/local/lib/

WORKDIR /app

RUN drogon_ctl create project ParallelSystems

WORKDIR /app/ParallelSystems

RUN cp CMakeLists.txt ..

COPY . /app/ParallelSystems

RUN cp -f ../CMakeLists.txt ./

RUN cmake -Bbuild -S. && cmake --build build

RUN ls ./build/

CMD ["./build/ParallelSystems"]

EXPOSE 8848