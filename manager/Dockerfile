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
    gdb \
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

#RUN git clone https://github.com/alanxz/SimpleAmqpClient.git && \
#    cd SimpleAmqpClient && mkdir simpleamqpclient-build &&  \
#    cd simpleamqpclient-build && cmake -DCMAKE_BUILD_TYPE=Debug .. && sudo make install

RUN git clone https://github.com/CopernicaMarketingSoftware/AMQP-CPP.git && \
    cd AMQP-CPP && mkdir build && cd build && \
    cmake .. -DAMQP-CPP_BUILD_SHARED=ON -DAMQP-CPP_LINUX_TCP=ON && \
    sudo cmake --build . --target install

RUN ls /usr/local/include

RUN ls /usr/local/lib

RUN ln -s /usr/include/jsoncpp/json/ /usr/include/json

# Update linker cache and set library path
RUN sudo echo "/usr/local/lib" > /etc/ld.so.conf.d/local.conf && \
    sudo ldconfig && \
    sudo echo "LD_LIBRARY_PATH=/usr/local/lib" >> /etc/environment

ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

WORKDIR /app/ParallelSystems
#
#RUN drogon_ctl create project ParallelSystems
#
#WORKDIR /app/ParallelSystems
#
#COPY . /app/ParallelSystems
#
#RUN cd build && cmake /app/ParallelSystems -DCMAKE_VERBOSE_MAKEFILE=ON && make
#
#RUN ls /usr/local/lib
#
#CMD ["/app/ParallelSystems/build/ParallelSystems"]

EXPOSE 8848:8848

EXPOSE 1234:1234

