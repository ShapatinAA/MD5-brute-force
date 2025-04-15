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
    git \
    librabbitmq-dev  \
    libboost-dev

RUN git clone https://github.com/alanxz/SimpleAmqpClient.git && \
    cd SimpleAmqpClient && mkdir simpleamqpclient-build &&  \
    cd simpleamqpclient-build && cmake .. && sudo make install

RUN ln -s /usr/include/jsoncpp/json/ /usr/include/json

WORKDIR /app

RUN drogon_ctl create project ParallelSystems

WORKDIR /app/ParallelSystems

RUN cp CMakeLists.txt ..

COPY . /app/ParallelSystems

RUN cp -f ../CMakeLists.txt ./

RUN cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j4 -verbose

RUN ls ./build/

CMD ["./build/ParallelSystems"]

EXPOSE 8848