#Base image
FROM restreamio/gstreamer:1.18.4.0-prod
RUN apt-get update
RUN apt-get install -y cmake pkg-config libglib2.0 libunwind-dev libdw-dev

COPY . .

RUN mkdir build && cd build && cmake .. && make
RUN cd build && ./dyn_video_pipeline