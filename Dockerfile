FROM ubuntu:22.04

# 跳過 GPG 金鑰檢查
RUN apt-get update -o Acquire::AllowInsecureRepositories=true \
    && apt-get install -y --allow-unauthenticated \
    g++ \
    cmake \
    libboost-all-dev \
    make \
    curl \
    && rm -rf /var/lib/apt/lists/*

# 下載 json.hpp
RUN curl -L -o /usr/include/nlohmann/json.hpp \
    https://github.com/nlohmann/json/releases/download/v3.11.2/json.hpp

WORKDIR /app
COPY . .

RUN g++ -std=c++17 -O2 server.cpp -o server -lpthread

EXPOSE 8080
CMD ["./server"]